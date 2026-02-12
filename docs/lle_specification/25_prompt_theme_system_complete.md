# Prompt and Theme System Complete Specification

**Document**: 25_prompt_theme_system_complete.md  
**Version**: 1.0.0  
**Date**: 2025-01-27  
**Status**: Implementation-Ready Specification  
**Classification**: Critical Core Component  

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Core Type Definitions](#3-core-type-definitions)
4. [Theme Registry System](#4-theme-registry-system)
5. [Segment Architecture](#5-segment-architecture)
6. [Template Engine](#6-template-engine)
7. [Async Data Providers](#7-async-data-providers)
8. [Event Integration](#8-event-integration)
9. [Display Integration](#9-display-integration)
10. [Configuration System](#10-configuration-system)
11. [Built-in Themes and Segments](#11-built-in-themes-and-segments)
12. [Transient Prompt System](#12-transient-prompt-system)
13. [External Prompt Integration](#13-external-prompt-integration)
14. [Performance Requirements](#14-performance-requirements)
15. [Error Handling](#15-error-handling)
16. [Testing and Validation](#16-testing-and-validation)

---

## 1. Executive Summary

### 1.1 Purpose

The Prompt and Theme System provides a unified, first-class architecture for prompt generation and theming in the Lush Line Editor (LLE). This specification supersedes the separate prompt.c and themes.c implementations, unifying them into a cohesive, event-driven, extensible system where user-created themes and prompts are equal citizens with built-in defaults.

### 1.2 Key Features

- **Unified Prompt/Theme Architecture**: Single cohesive system, not separate components
- **First-Class User Extensibility**: User themes registered identically to built-in themes
- **Event-Driven Updates**: Cache invalidation via LLE events, not time-based polling
- **Async Data Providers**: Non-blocking git status and expensive operations
- **Segment-Based Composition**: Modular, reusable prompt components
- **Template Engine**: Expressive format strings with conditionals
- **Transient Prompt**: Previous prompts simplified to reduce visual clutter
- **Traditional Variable Support**: PS1/PS2/RPROMPT integration
- **External Prompt Support**: Architecture supports starship/oh-my-posh integration
- **screen_buffer Integration**: Renders through existing display infrastructure

### 1.3 Critical Design Principles

1. **First-Class Citizenship**: All themes (built-in and user) use identical registration
2. **Event-Driven**: No time-based polling; react to shell events via Spec 26 event hub
3. **Non-Blocking**: Expensive operations (git) run asynchronously
4. **Zero External Dependencies**: No Lua, no external parsers, pthreads only
5. **Performance Parity**: Equal or better than current prompt/theme system
6. **Future-Proof**: Architecture supports real-time updates without requiring them
7. **screen_buffer Native**: All rendering through virtual screen infrastructure

### 1.4 Problems Solved

- **Issue #16**: Stale git prompt after directory change (event-driven invalidation)
- **Issue #20**: Prompt/theme integration blockers (unified architecture)
- **Separation of Concerns**: Prompts and themes unified conceptually
- **User Extensibility**: No more hardcoded-only themes

---

## 2. Architecture Overview

### 2.1 System Component Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      LLE Prompt/Theme System                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                        Theme Registry                                │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                  │    │
│  │  │ Built-in    │  │ User        │  │ Active      │                  │    │
│  │  │ Themes      │  │ Themes      │  │ Theme Ptr   │                  │    │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                  │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                       Prompt Composer                                │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                  │    │
│  │  │ Segment     │  │ Template    │  │ Layout      │                  │    │
│  │  │ Registry    │  │ Engine      │  │ Manager     │                  │    │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                  │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                      Data Providers                                  │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌────────────┐  │    │
│  │  │ Git         │  │ Directory   │  │ User        │  │ Time       │  │    │
│  │  │ (async)     │  │ (sync)      │  │ (cached)    │  │ (sync)     │  │    │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └────────────┘  │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                   Shell Event Hub Integration (Spec 26)              │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                  │    │
│  │  │ DIR_CHANGED │  │ PRE_COMMAND │  │ POST_COMMAND│                  │    │
│  │  │ Handler     │  │ Handler     │  │ Handler     │                  │    │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                  │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                      Display Integration                             │    │
│  │                      (screen_buffer)                                 │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Data Flow

```
User Action (cd, command, etc.)
        │
        ▼
┌───────────────────────────────┐
│ Shell Event Hub (Spec 26)     │
│  lle_fire_directory_changed() │──── DIR_CHANGED ────┐
│  lle_fire_pre_command()       │──── PRE_COMMAND ────┤
│  lle_fire_post_command()      │──── POST_COMMAND ───┤
└───────────────────────────────┘                     │
                                                      ▼
                                            ┌─────────────────────┐
                                            │ Prompt Handlers     │
                                            │ (cache invalidation)│
                                            └─────────────────────┘
                                             │
                                             ▼
                                   ┌─────────────────────┐
                                   │ Async Data Request  │
                                   │ (background thread) │
                                   └─────────────────────┘
                                             │
                                             ▼
┌───────────────────┐            ┌─────────────────────┐
│ Prompt Composer   │◄───────────│ Data Ready Event    │
│                   │            └─────────────────────┘
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ Template Engine   │
│ (segment render)  │
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ screen_buffer     │
│ (display)         │
└───────────────────┘
```

### 2.3 Configuration Hierarchy

```
Priority (highest to lowest):

1. Shell Variable Override ($LUSH_PROMPT)
   └── Immediate effect, no file needed
   
2. User Config File (~/.config/lush/prompt.toml)
   └── Full control, persistent
   
3. System Config (/etc/lush/prompt.toml)
   └── System-wide defaults
   
4. Built-in Defaults (compiled in)
   └── Always available fallback
   
Special Mode:
- use_theme_prompt = false → LLE respects user PS1/PS2, no overwrite
- external_prompt = "starship" → Defer to external program
```

---

## 3. Core Type Definitions

### 3.1 Result and Error Types

```c
/**
 * Prompt/Theme system result codes
 */
typedef enum lle_prompt_result {
    LLE_PROMPT_SUCCESS = 0,
    LLE_PROMPT_ERROR_INVALID_PARAM,
    LLE_PROMPT_ERROR_NOT_INITIALIZED,
    LLE_PROMPT_ERROR_MEMORY_ALLOCATION,
    LLE_PROMPT_ERROR_THEME_NOT_FOUND,
    LLE_PROMPT_ERROR_SEGMENT_NOT_FOUND,
    LLE_PROMPT_ERROR_TEMPLATE_PARSE,
    LLE_PROMPT_ERROR_RENDER_FAILED,
    LLE_PROMPT_ERROR_CONFIG_PARSE,
    LLE_PROMPT_ERROR_ASYNC_TIMEOUT,
    LLE_PROMPT_ERROR_REGISTRY_FULL,
    LLE_PROMPT_ERROR_DUPLICATE_NAME,
    LLE_PROMPT_ERROR_INHERITANCE_CYCLE,
    LLE_PROMPT_ERROR_EXTERNAL_PROVIDER
} lle_prompt_result_t;
```

### 3.2 Capability Flags

```c
/**
 * Theme capability flags
 */
typedef enum lle_theme_capability {
    LLE_THEME_CAP_NONE           = 0,
    LLE_THEME_CAP_256_COLOR      = (1 << 0),  // Uses 256-color palette
    LLE_THEME_CAP_TRUE_COLOR     = (1 << 1),  // Uses 24-bit true color
    LLE_THEME_CAP_POWERLINE      = (1 << 2),  // Requires powerline fonts
    LLE_THEME_CAP_NERD_FONT      = (1 << 3),  // Requires nerd fonts
    LLE_THEME_CAP_UNICODE        = (1 << 4),  // Uses Unicode symbols
    LLE_THEME_CAP_ASCII_FALLBACK = (1 << 5),  // Has ASCII fallbacks
    LLE_THEME_CAP_MULTILINE      = (1 << 6),  // Multi-line prompt
    LLE_THEME_CAP_RIGHT_PROMPT   = (1 << 7),  // Uses right prompt
    LLE_THEME_CAP_TRANSIENT      = (1 << 8),  // Supports transient prompt
    LLE_THEME_CAP_ASYNC_SEGMENTS = (1 << 9),  // Has async segments
    LLE_THEME_CAP_INHERITABLE    = (1 << 10)  // Can be inherited from
} lle_theme_capability_t;

/**
 * Segment capability flags
 */
typedef enum lle_segment_capability {
    LLE_SEG_CAP_NONE        = 0,
    LLE_SEG_CAP_ASYNC       = (1 << 0),  // Requires async data
    LLE_SEG_CAP_CACHEABLE   = (1 << 1),  // Output can be cached
    LLE_SEG_CAP_EXPENSIVE   = (1 << 2),  // May be slow, respect timeouts
    LLE_SEG_CAP_THEME_AWARE = (1 << 3),  // Uses theme colors
    LLE_SEG_CAP_DYNAMIC     = (1 << 4),  // Content changes frequently
    LLE_SEG_CAP_OPTIONAL    = (1 << 5),  // Can be hidden if no data
    LLE_SEG_CAP_PROPERTIES  = (1 << 6)   // Exposes sub-properties
} lle_segment_capability_t;
```

### 3.3 Color System Types

```c
/**
 * Maximum sizes for color and theme strings
 */
#define LLE_COLOR_CODE_MAX      32
#define LLE_THEME_NAME_MAX      64
#define LLE_THEME_DESC_MAX      256
#define LLE_TEMPLATE_MAX        1024
#define LLE_SEGMENT_NAME_MAX    32
#define LLE_PROPERTY_NAME_MAX   32
#define LLE_PROMPT_OUTPUT_MAX   4096

/**
 * Color representation modes
 */
typedef enum lle_color_mode {
    LLE_COLOR_MODE_NONE,       // No color
    LLE_COLOR_MODE_BASIC,      // 8 basic ANSI colors
    LLE_COLOR_MODE_256,        // 256-color palette
    LLE_COLOR_MODE_TRUE        // 24-bit true color
} lle_color_mode_t;

/**
 * Single color value (supports all modes)
 */
typedef struct lle_color {
    lle_color_mode_t mode;
    union {
        uint8_t basic;         // 0-7 for basic colors
        uint8_t palette;       // 0-255 for 256-color
        struct {
            uint8_t r, g, b;   // RGB for true color
        } rgb;
    } value;
    bool bold;
    bool italic;
    bool underline;
    bool dim;
} lle_color_t;

/**
 * Semantic color scheme for themes
 * Each color has foreground and optional background
 */
typedef struct lle_color_scheme {
    // Core semantic colors
    lle_color_t primary;
    lle_color_t secondary;
    lle_color_t success;
    lle_color_t warning;
    lle_color_t error;
    lle_color_t info;
    
    // Text colors
    lle_color_t text;
    lle_color_t text_dim;
    lle_color_t text_bright;
    
    // Structural colors
    lle_color_t border;
    lle_color_t background;
    lle_color_t highlight;
    
    // Git-specific colors
    lle_color_t git_clean;
    lle_color_t git_dirty;
    lle_color_t git_staged;
    lle_color_t git_untracked;
    lle_color_t git_branch;
    lle_color_t git_ahead;
    lle_color_t git_behind;
    
    // Path colors
    lle_color_t path_home;
    lle_color_t path_root;
    lle_color_t path_normal;
    lle_color_t path_separator;
    
    // Status colors
    lle_color_t status_ok;
    lle_color_t status_error;
    lle_color_t status_running;
} lle_color_scheme_t;
```

### 3.4 Symbol Compatibility Types

```c
/**
 * Symbol compatibility mode
 */
typedef enum lle_symbol_mode {
    LLE_SYMBOL_MODE_UNICODE,   // Full Unicode symbols
    LLE_SYMBOL_MODE_ASCII,     // ASCII-only fallbacks
    LLE_SYMBOL_MODE_AUTO       // Auto-detect terminal capability
} lle_symbol_mode_t;

/**
 * Symbol mapping for Unicode to ASCII fallback
 */
typedef struct lle_symbol_mapping {
    const char *unicode;       // Unicode symbol (e.g., "➜")
    const char *ascii;         // ASCII fallback (e.g., "->")
    const char *description;   // Human-readable description
} lle_symbol_mapping_t;

/**
 * Symbol set for a theme
 */
typedef struct lle_symbol_set {
    char prompt_symbol[8];           // Main prompt symbol (e.g., "❯")
    char prompt_symbol_ascii[8];     // ASCII fallback (e.g., ">")
    char continuation_symbol[8];     // PS2 symbol (e.g., "…")
    char continuation_ascii[8];      // ASCII fallback (e.g., "...")
    char separator_left[8];          // Powerline left (e.g., "")
    char separator_right[8];         // Powerline right (e.g., "")
    char branch_symbol[8];           // Git branch (e.g., "")
    char staged_symbol[8];           // Staged changes (e.g., "●")
    char unstaged_symbol[8];         // Unstaged changes (e.g., "○")
    char untracked_symbol[8];        // Untracked files (e.g., "?")
    char ahead_symbol[8];            // Commits ahead (e.g., "↑")
    char behind_symbol[8];           // Commits behind (e.g., "↓")
    char stash_symbol[8];            // Stashes (e.g., "≡")
    char conflict_symbol[8];         // Merge conflicts (e.g., "!")
    char directory_symbol[8];        // Directory (e.g., "")
    char home_symbol[8];             // Home directory (e.g., "~")
    char root_symbol[8];             // Root user (e.g., "#")
    char error_symbol[8];            // Command error (e.g., "✗")
    char success_symbol[8];          // Command success (e.g., "✓")
    char time_symbol[8];             // Time display (e.g., "")
    char jobs_symbol[8];             // Background jobs (e.g., "⚙")
} lle_symbol_set_t;
```

### 3.5 Theme Definition

```c
/**
 * Theme category for organization
 */
typedef enum lle_theme_category {
    LLE_THEME_CATEGORY_MINIMAL,      // Ultra-simple themes
    LLE_THEME_CATEGORY_CLASSIC,      // Traditional shell style
    LLE_THEME_CATEGORY_MODERN,       // Modern with Unicode
    LLE_THEME_CATEGORY_POWERLINE,    // Powerline-style
    LLE_THEME_CATEGORY_PROFESSIONAL, // Business/corporate
    LLE_THEME_CATEGORY_CREATIVE,     // Colorful/artistic
    LLE_THEME_CATEGORY_CUSTOM        // User-defined
} lle_theme_category_t;

/**
 * Theme source type
 */
typedef enum lle_theme_source {
    LLE_THEME_SOURCE_BUILTIN,        // Compiled into binary
    LLE_THEME_SOURCE_SYSTEM,         // /etc/lush/themes/
    LLE_THEME_SOURCE_USER,           // ~/.config/lush/themes/
    LLE_THEME_SOURCE_RUNTIME         // Registered at runtime
} lle_theme_source_t;

/**
 * Prompt layout configuration
 */
typedef struct lle_prompt_layout {
    char left_format[LLE_TEMPLATE_MAX];       // Left prompt format string
    char right_format[LLE_TEMPLATE_MAX];      // Right prompt format string (RPROMPT)
    char continuation_format[LLE_TEMPLATE_MAX]; // PS2 format string
    char transient_format[LLE_TEMPLATE_MAX];  // Simplified prompt for history
    
    bool enable_right_prompt;        // Show RPROMPT
    bool enable_transient;           // Simplify old prompts
    bool enable_multiline;           // Multi-line primary prompt
    bool compact_mode;               // Reduce spacing
    
    uint8_t newline_before;          // Newlines before prompt
    uint8_t newline_after;           // Newlines after prompt (before input)
} lle_prompt_layout_t;

/**
 * Complete theme definition
 * This is the primary structure for both built-in and user themes
 */
typedef struct lle_theme {
    // Identity
    char name[LLE_THEME_NAME_MAX];
    char description[LLE_THEME_DESC_MAX];
    char author[LLE_THEME_NAME_MAX];
    char version[16];
    
    // Classification
    lle_theme_category_t category;
    lle_theme_source_t source;
    uint32_t capabilities;           // lle_theme_capability_t flags
    
    // Inheritance
    char inherits_from[LLE_THEME_NAME_MAX];  // Parent theme name (empty if none)
    
    // Visual configuration
    lle_color_scheme_t colors;
    lle_symbol_set_t symbols;
    lle_prompt_layout_t layout;
    
    // Segment configuration
    char enabled_segments[32][LLE_SEGMENT_NAME_MAX];  // List of enabled segments
    size_t enabled_segment_count;
    
    // Syntax highlighting integration
    bool override_syntax_colors;     // If true, use theme's syntax colors
    lle_color_scheme_t syntax_colors; // Syntax highlighting overrides
    
    // Runtime state (not persisted)
    struct lle_theme *parent;        // Resolved parent theme pointer
    bool is_active;                  // Currently active theme
    uint64_t load_time_ns;           // Time taken to load
} lle_theme_t;
```

### 3.6 Segment Definition

```c
/**
 * Forward declarations
 */
typedef struct lle_prompt_context lle_prompt_context_t;
typedef struct lle_async_request lle_async_request_t;
typedef struct lle_async_response lle_async_response_t;

/**
 * Segment render result
 */
typedef struct lle_segment_output {
    char content[512];               // Rendered content with ANSI codes
    size_t content_len;              // Length in bytes
    size_t visual_width;             // Display width in columns
    bool is_empty;                   // No content to display
    bool needs_separator;            // Should have separator after
} lle_segment_output_t;

/**
 * Complete segment definition
 * Segments are the building blocks of prompts
 */
typedef struct lle_prompt_segment {
    // Identity
    char name[LLE_SEGMENT_NAME_MAX];
    char description[LLE_THEME_DESC_MAX];
    
    // Capabilities
    uint32_t capabilities;           // lle_segment_capability_t flags
    
    // Lifecycle functions
    lle_prompt_result_t (*init)(struct lle_prompt_segment *self);
    void (*cleanup)(struct lle_prompt_segment *self);
    
    // Visibility and rendering
    bool (*is_enabled)(const struct lle_prompt_segment *self);
    bool (*is_visible)(const struct lle_prompt_segment *self,
                       const lle_prompt_context_t *ctx);
    lle_prompt_result_t (*render)(const struct lle_prompt_segment *self,
                                  const lle_prompt_context_t *ctx,
                                  const lle_theme_t *theme,
                                  lle_segment_output_t *output);
    
    // Async support (for expensive segments like git)
    lle_prompt_result_t (*request_async_data)(struct lle_prompt_segment *self,
                                              lle_async_request_t *request);
    lle_prompt_result_t (*on_async_data_ready)(struct lle_prompt_segment *self,
                                               const lle_async_response_t *response);
    
    // Property access for templates (e.g., ${git.branch})
    const char *(*get_property)(const struct lle_prompt_segment *self,
                                const char *property_name);
    
    // Cache control
    bool (*is_cache_valid)(const struct lle_prompt_segment *self);
    void (*invalidate_cache)(struct lle_prompt_segment *self);
    
    // Segment-private state
    void *state;
    
    // Statistics
    uint64_t total_render_time_ns;
    uint64_t render_count;
    uint64_t cache_hit_count;
} lle_prompt_segment_t;
```

### 3.7 Prompt Context

```c
/**
 * Prompt context passed to segments during rendering
 * Contains all environmental information needed to render
 */
typedef struct lle_prompt_context {
    // Shell state
    int last_exit_code;              // Exit code of last command
    int last_pipe_status[16];        // Exit codes of pipeline components
    size_t pipe_status_count;
    uint64_t last_cmd_duration_ms;   // Duration of last command
    int background_job_count;        // Number of background jobs
    
    // User information
    char username[64];
    char hostname[256];
    uid_t uid;
    bool is_root;
    
    // Directory information
    char cwd[PATH_MAX];              // Current working directory
    char cwd_display[PATH_MAX];      // Display version (~ for home)
    char home_dir[PATH_MAX];
    bool cwd_is_home;
    bool cwd_is_writable;
    bool cwd_is_git_repo;
    
    // Terminal information
    int terminal_width;
    int terminal_height;
    bool has_true_color;
    bool has_256_color;
    bool has_unicode;
    lle_symbol_mode_t symbol_mode;
    
    // Time information
    time_t current_time;
    struct tm current_tm;
    
    // Active theme
    const lle_theme_t *theme;
    
    // Keymap state (for vi mode indicator)
    char keymap[32];                 // "viins", "vicmd", "emacs"
    
    // Shell-specific
    int shlvl;                       // Shell nesting level
    bool is_ssh_session;
    bool is_container;
    char container_name[64];
    
    // Async state
    bool async_data_pending;         // Waiting for async data
    bool async_data_ready;           // Async data available
} lle_prompt_context_t;
```

---

## 4. Theme Registry System

### 4.1 Registry Structure

```c
/**
 * Theme registry - unified storage for all themes
 * Built-in and user themes use identical registration
 */
typedef struct lle_theme_registry {
    // Theme storage
    lle_theme_t **themes;            // Array of theme pointers
    size_t count;                    // Number of registered themes
    size_t capacity;                 // Allocated capacity
    
    // Active theme tracking
    lle_theme_t *active_theme;       // Currently active theme
    char active_theme_name[LLE_THEME_NAME_MAX];
    
    // Default theme (fallback)
    char default_theme_name[LLE_THEME_NAME_MAX];
    
    // Lookup acceleration
    void *name_index;                // Hash table for O(1) lookup by name
    
    // State
    bool initialized;
    pthread_rwlock_t lock;           // Reader-writer lock for thread safety
    
    // Statistics
    size_t builtin_count;
    size_t user_count;
    uint64_t total_switches;
} lle_theme_registry_t;
```

### 4.2 Registry Initialization

```c
/**
 * Initialize the theme registry
 * Must be called before any other theme operations
 */
lle_prompt_result_t lle_theme_registry_init(lle_theme_registry_t **registry) {
    if (!registry) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    lle_theme_registry_t *reg = calloc(1, sizeof(lle_theme_registry_t));
    if (!reg) {
        return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    }
    
    // Initial capacity for themes
    reg->capacity = 32;
    reg->themes = calloc(reg->capacity, sizeof(lle_theme_t *));
    if (!reg->themes) {
        free(reg);
        return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    }
    
    // Initialize lock
    if (pthread_rwlock_init(&reg->lock, NULL) != 0) {
        free(reg->themes);
        free(reg);
        return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    }
    
    // Set default theme name
    strncpy(reg->default_theme_name, "minimal", LLE_THEME_NAME_MAX - 1);
    
    reg->initialized = true;
    *registry = reg;
    
    return LLE_PROMPT_SUCCESS;
}
```

### 4.3 Theme Registration

```c
/**
 * Register a theme in the registry
 * Works identically for built-in and user themes (first-class citizenship)
 * 
 * @param registry The theme registry
 * @param theme    Theme to register (ownership transferred to registry)
 * @return LLE_PROMPT_SUCCESS or error code
 */
lle_prompt_result_t lle_theme_registry_register(
    lle_theme_registry_t *registry,
    lle_theme_t *theme
) {
    if (!registry || !theme || !registry->initialized) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    if (strlen(theme->name) == 0) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    pthread_rwlock_wrlock(&registry->lock);
    
    // Check for duplicate name
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->themes[i]->name, theme->name) == 0) {
            pthread_rwlock_unlock(&registry->lock);
            return LLE_PROMPT_ERROR_DUPLICATE_NAME;
        }
    }
    
    // Expand capacity if needed
    if (registry->count >= registry->capacity) {
        size_t new_capacity = registry->capacity * 2;
        lle_theme_t **new_themes = realloc(
            registry->themes, 
            new_capacity * sizeof(lle_theme_t *)
        );
        if (!new_themes) {
            pthread_rwlock_unlock(&registry->lock);
            return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
        }
        registry->themes = new_themes;
        registry->capacity = new_capacity;
    }
    
    // Resolve inheritance if specified
    if (strlen(theme->inherits_from) > 0) {
        lle_prompt_result_t result = lle_theme_resolve_inheritance(
            registry, theme
        );
        if (result != LLE_PROMPT_SUCCESS) {
            pthread_rwlock_unlock(&registry->lock);
            return result;
        }
    }
    
    // Register theme
    registry->themes[registry->count] = theme;
    registry->count++;
    
    // Update statistics
    if (theme->source == LLE_THEME_SOURCE_BUILTIN) {
        registry->builtin_count++;
    } else {
        registry->user_count++;
    }
    
    pthread_rwlock_unlock(&registry->lock);
    return LLE_PROMPT_SUCCESS;
}
```

### 4.4 Theme Inheritance

```c
/**
 * Resolve theme inheritance chain
 * Copies unset values from parent theme(s)
 * Detects and prevents inheritance cycles
 */
lle_prompt_result_t lle_theme_resolve_inheritance(
    lle_theme_registry_t *registry,
    lle_theme_t *theme
) {
    if (!registry || !theme) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    if (strlen(theme->inherits_from) == 0) {
        return LLE_PROMPT_SUCCESS;  // No inheritance
    }
    
    // Find parent theme
    lle_theme_t *parent = NULL;
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->themes[i]->name, theme->inherits_from) == 0) {
            parent = registry->themes[i];
            break;
        }
    }
    
    if (!parent) {
        return LLE_PROMPT_ERROR_THEME_NOT_FOUND;
    }
    
    // Check for inheritance cycle (max depth 10)
    lle_theme_t *ancestor = parent;
    for (int depth = 0; depth < 10 && ancestor; depth++) {
        if (strcmp(ancestor->name, theme->name) == 0) {
            return LLE_PROMPT_ERROR_INHERITANCE_CYCLE;
        }
        ancestor = ancestor->parent;
    }
    
    // Link parent
    theme->parent = parent;
    
    // Inherit colors (only if not explicitly set)
    lle_color_scheme_t *child_colors = &theme->colors;
    const lle_color_scheme_t *parent_colors = &parent->colors;
    
    // Macro to inherit individual color if child's is unset
    #define INHERIT_COLOR(field) \
        if (child_colors->field.mode == LLE_COLOR_MODE_NONE) { \
            child_colors->field = parent_colors->field; \
        }
    
    INHERIT_COLOR(primary);
    INHERIT_COLOR(secondary);
    INHERIT_COLOR(success);
    INHERIT_COLOR(warning);
    INHERIT_COLOR(error);
    INHERIT_COLOR(info);
    INHERIT_COLOR(text);
    INHERIT_COLOR(text_dim);
    INHERIT_COLOR(git_clean);
    INHERIT_COLOR(git_dirty);
    INHERIT_COLOR(git_branch);
    INHERIT_COLOR(path_home);
    INHERIT_COLOR(path_normal);
    
    #undef INHERIT_COLOR
    
    // Inherit symbols if empty
    #define INHERIT_SYMBOL(field) \
        if (strlen(theme->symbols.field) == 0) { \
            strncpy(theme->symbols.field, parent->symbols.field, \
                    sizeof(theme->symbols.field) - 1); \
        }
    
    INHERIT_SYMBOL(prompt_symbol);
    INHERIT_SYMBOL(continuation_symbol);
    INHERIT_SYMBOL(branch_symbol);
    INHERIT_SYMBOL(staged_symbol);
    INHERIT_SYMBOL(unstaged_symbol);
    
    #undef INHERIT_SYMBOL
    
    // Inherit layout if not set
    if (strlen(theme->layout.left_format) == 0) {
        strncpy(theme->layout.left_format, parent->layout.left_format,
                LLE_TEMPLATE_MAX - 1);
    }
    if (strlen(theme->layout.continuation_format) == 0) {
        strncpy(theme->layout.continuation_format, 
                parent->layout.continuation_format, LLE_TEMPLATE_MAX - 1);
    }
    if (strlen(theme->layout.transient_format) == 0) {
        strncpy(theme->layout.transient_format,
                parent->layout.transient_format, LLE_TEMPLATE_MAX - 1);
    }
    
    // Inherit capabilities (additive)
    theme->capabilities |= (parent->capabilities & LLE_THEME_CAP_INHERITABLE);
    
    return LLE_PROMPT_SUCCESS;
}
```

### 4.5 Theme Lookup and Activation

```c
/**
 * Find theme by name
 */
lle_theme_t *lle_theme_registry_find(
    lle_theme_registry_t *registry,
    const char *name
) {
    if (!registry || !name || !registry->initialized) {
        return NULL;
    }
    
    pthread_rwlock_rdlock(&registry->lock);
    
    lle_theme_t *found = NULL;
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->themes[i]->name, name) == 0) {
            found = registry->themes[i];
            break;
        }
    }
    
    pthread_rwlock_unlock(&registry->lock);
    return found;
}

/**
 * Set the active theme
 * Publishes LLE_EVENT_THEME_CHANGE event
 */
lle_prompt_result_t lle_theme_registry_set_active(
    lle_theme_registry_t *registry,
    const char *name
) {
    if (!registry || !name || !registry->initialized) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    lle_theme_t *theme = lle_theme_registry_find(registry, name);
    if (!theme) {
        return LLE_PROMPT_ERROR_THEME_NOT_FOUND;
    }
    
    pthread_rwlock_wrlock(&registry->lock);
    
    // Deactivate current theme
    if (registry->active_theme) {
        registry->active_theme->is_active = false;
    }
    
    // Activate new theme
    registry->active_theme = theme;
    theme->is_active = true;
    strncpy(registry->active_theme_name, name, LLE_THEME_NAME_MAX - 1);
    registry->total_switches++;
    
    pthread_rwlock_unlock(&registry->lock);
    
    // Publish theme change event (for cache invalidation, redraw, etc.)
    lle_event_t event = {
        .type = LLE_EVENT_THEME_CHANGE,
        .data.theme_change = {
            .theme = theme,
            .theme_name = registry->active_theme_name
        }
    };
    lle_event_publish(&event);
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Get the currently active theme
 */
lle_theme_t *lle_theme_registry_get_active(lle_theme_registry_t *registry) {
    if (!registry || !registry->initialized) {
        return NULL;
    }
    
    pthread_rwlock_rdlock(&registry->lock);
    lle_theme_t *active = registry->active_theme;
    pthread_rwlock_unlock(&registry->lock);
    
    return active;
}
```

---

## 5. Segment Architecture

### 5.1 Segment Registry

```c
/**
 * Segment registry - stores all available prompt segments
 */
typedef struct lle_segment_registry {
    lle_prompt_segment_t **segments;
    size_t count;
    size_t capacity;
    
    // Lookup by name
    void *name_index;
    
    bool initialized;
    pthread_mutex_t lock;
} lle_segment_registry_t;

/**
 * Initialize segment registry
 */
lle_prompt_result_t lle_segment_registry_init(lle_segment_registry_t **registry) {
    if (!registry) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    lle_segment_registry_t *reg = calloc(1, sizeof(lle_segment_registry_t));
    if (!reg) {
        return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    }
    
    reg->capacity = 64;
    reg->segments = calloc(reg->capacity, sizeof(lle_prompt_segment_t *));
    if (!reg->segments) {
        free(reg);
        return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    }
    
    pthread_mutex_init(&reg->lock, NULL);
    reg->initialized = true;
    *registry = reg;
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Register a segment
 */
lle_prompt_result_t lle_segment_registry_register(
    lle_segment_registry_t *registry,
    lle_prompt_segment_t *segment
) {
    if (!registry || !segment || !registry->initialized) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&registry->lock);
    
    // Check duplicate
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->segments[i]->name, segment->name) == 0) {
            pthread_mutex_unlock(&registry->lock);
            return LLE_PROMPT_ERROR_DUPLICATE_NAME;
        }
    }
    
    // Expand if needed
    if (registry->count >= registry->capacity) {
        size_t new_cap = registry->capacity * 2;
        lle_prompt_segment_t **new_segs = realloc(
            registry->segments, new_cap * sizeof(lle_prompt_segment_t *)
        );
        if (!new_segs) {
            pthread_mutex_unlock(&registry->lock);
            return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
        }
        registry->segments = new_segs;
        registry->capacity = new_cap;
    }
    
    // Initialize segment
    if (segment->init) {
        lle_prompt_result_t result = segment->init(segment);
        if (result != LLE_PROMPT_SUCCESS) {
            pthread_mutex_unlock(&registry->lock);
            return result;
        }
    }
    
    registry->segments[registry->count] = segment;
    registry->count++;
    
    pthread_mutex_unlock(&registry->lock);
    return LLE_PROMPT_SUCCESS;
}

/**
 * Find segment by name
 */
lle_prompt_segment_t *lle_segment_registry_find(
    lle_segment_registry_t *registry,
    const char *name
) {
    if (!registry || !name) return NULL;
    
    pthread_mutex_lock(&registry->lock);
    
    lle_prompt_segment_t *found = NULL;
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->segments[i]->name, name) == 0) {
            found = registry->segments[i];
            break;
        }
    }
    
    pthread_mutex_unlock(&registry->lock);
    return found;
}
```

### 5.2 Built-in Segment: Directory

```c
/**
 * Directory segment state
 */
typedef struct lle_segment_directory_state {
    char cached_cwd[PATH_MAX];
    char cached_display[PATH_MAX];
    int truncate_to;              // Number of path components to show
    bool show_readonly_indicator;
    bool use_home_symbol;
} lle_segment_directory_state_t;

/**
 * Directory segment implementation
 */
static lle_prompt_result_t segment_directory_init(lle_prompt_segment_t *self) {
    lle_segment_directory_state_t *state = calloc(1, sizeof(*state));
    if (!state) return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    
    state->truncate_to = 3;
    state->show_readonly_indicator = true;
    state->use_home_symbol = true;
    
    self->state = state;
    return LLE_PROMPT_SUCCESS;
}

static void segment_directory_cleanup(lle_prompt_segment_t *self) {
    free(self->state);
    self->state = NULL;
}

static bool segment_directory_is_visible(
    const lle_prompt_segment_t *self,
    const lle_prompt_context_t *ctx
) {
    (void)self;
    return strlen(ctx->cwd) > 0;
}

static lle_prompt_result_t segment_directory_render(
    const lle_prompt_segment_t *self,
    const lle_prompt_context_t *ctx,
    const lle_theme_t *theme,
    lle_segment_output_t *output
) {
    lle_segment_directory_state_t *state = self->state;
    
    // Determine display path
    const char *display = ctx->cwd_display;
    if (strlen(display) == 0) {
        display = ctx->cwd;
    }
    
    // Get appropriate color
    const lle_color_t *color;
    if (ctx->cwd_is_home) {
        color = &theme->colors.path_home;
    } else if (ctx->is_root) {
        color = &theme->colors.path_root;
    } else {
        color = &theme->colors.path_normal;
    }
    
    // Build output with color
    char color_code[LLE_COLOR_CODE_MAX];
    lle_color_to_ansi(color, color_code, sizeof(color_code));
    
    // Truncate path if needed
    char truncated[PATH_MAX];
    lle_path_truncate(display, truncated, sizeof(truncated), state->truncate_to);
    
    snprintf(output->content, sizeof(output->content),
             "%s%s\033[0m", color_code, truncated);
    
    output->content_len = strlen(output->content);
    output->visual_width = lle_utf8_string_width(truncated, strlen(truncated));
    output->is_empty = false;
    output->needs_separator = true;
    
    return LLE_PROMPT_SUCCESS;
}

static const char *segment_directory_get_property(
    const lle_prompt_segment_t *self,
    const char *prop
) {
    lle_segment_directory_state_t *state = self->state;
    
    if (strcmp(prop, "full") == 0) {
        return state->cached_cwd;
    } else if (strcmp(prop, "display") == 0) {
        return state->cached_display;
    }
    return NULL;
}

/**
 * Directory segment definition
 */
static lle_prompt_segment_t segment_directory = {
    .name = "directory",
    .description = "Current working directory",
    .capabilities = LLE_SEG_CAP_CACHEABLE | LLE_SEG_CAP_THEME_AWARE | 
                    LLE_SEG_CAP_PROPERTIES,
    .init = segment_directory_init,
    .cleanup = segment_directory_cleanup,
    .is_enabled = NULL,  // Always enabled
    .is_visible = segment_directory_is_visible,
    .render = segment_directory_render,
    .get_property = segment_directory_get_property,
    .request_async_data = NULL,  // Sync segment
    .on_async_data_ready = NULL,
    .is_cache_valid = NULL,
    .invalidate_cache = NULL,
    .state = NULL
};
```

### 5.3 Built-in Segment: Git (Async)

```c
/**
 * Git segment state - cached git repository information
 */
typedef struct lle_segment_git_state {
    // Cached data
    char branch[256];
    char remote_branch[256];
    int staged_count;
    int unstaged_count;
    int untracked_count;
    int stash_count;
    int ahead_count;
    int behind_count;
    bool has_conflicts;
    char action[32];              // "rebase", "merge", "cherry-pick", etc.
    
    // Cache validity
    char cached_cwd[PATH_MAX];
    bool cache_valid;
    bool is_git_repo;
    
    // Async state
    bool async_pending;
    uint64_t request_id;
} lle_segment_git_state_t;

/**
 * Git segment init
 */
static lle_prompt_result_t segment_git_init(lle_prompt_segment_t *self) {
    lle_segment_git_state_t *state = calloc(1, sizeof(*state));
    if (!state) return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    
    state->cache_valid = false;
    state->is_git_repo = false;
    state->async_pending = false;
    
    self->state = state;
    return LLE_PROMPT_SUCCESS;
}

static void segment_git_cleanup(lle_prompt_segment_t *self) {
    free(self->state);
    self->state = NULL;
}

/**
 * Git segment visibility - only show in git repos
 */
static bool segment_git_is_visible(
    const lle_prompt_segment_t *self,
    const lle_prompt_context_t *ctx
) {
    lle_segment_git_state_t *state = self->state;
    return ctx->cwd_is_git_repo || state->is_git_repo;
}

/**
 * Request async git data
 */
static lle_prompt_result_t segment_git_request_async(
    lle_prompt_segment_t *self,
    lle_async_request_t *request
) {
    lle_segment_git_state_t *state = self->state;
    
    request->type = LLE_ASYNC_REQUEST_GIT_STATUS;
    request->segment = self;
    strncpy(request->cwd, state->cached_cwd, PATH_MAX - 1);
    request->timeout_ms = 500;  // 500ms timeout for git operations
    
    state->async_pending = true;
    state->request_id = request->id;
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Handle async data ready callback
 */
static lle_prompt_result_t segment_git_on_data_ready(
    lle_prompt_segment_t *self,
    const lle_async_response_t *response
) {
    lle_segment_git_state_t *state = self->state;
    
    if (response->id != state->request_id) {
        return LLE_PROMPT_SUCCESS;  // Stale response, ignore
    }
    
    state->async_pending = false;
    
    if (response->result != LLE_PROMPT_SUCCESS) {
        state->cache_valid = false;
        return response->result;
    }
    
    // Copy data from response
    const lle_git_status_data_t *git = &response->data.git_status;
    
    strncpy(state->branch, git->branch, sizeof(state->branch) - 1);
    strncpy(state->remote_branch, git->remote_branch, sizeof(state->remote_branch) - 1);
    state->staged_count = git->staged_count;
    state->unstaged_count = git->unstaged_count;
    state->untracked_count = git->untracked_count;
    state->stash_count = git->stash_count;
    state->ahead_count = git->ahead_count;
    state->behind_count = git->behind_count;
    state->has_conflicts = git->has_conflicts;
    strncpy(state->action, git->action, sizeof(state->action) - 1);
    
    state->is_git_repo = true;
    state->cache_valid = true;
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Render git segment
 */
static lle_prompt_result_t segment_git_render(
    const lle_prompt_segment_t *self,
    const lle_prompt_context_t *ctx,
    const lle_theme_t *theme,
    lle_segment_output_t *output
) {
    lle_segment_git_state_t *state = self->state;
    
    if (!state->cache_valid || !state->is_git_repo) {
        output->is_empty = true;
        return LLE_PROMPT_SUCCESS;
    }
    
    // Build git status string
    char status_buf[256] = {0};
    size_t pos = 0;
    
    // Branch name with color
    const lle_color_t *branch_color;
    if (state->staged_count > 0 || state->unstaged_count > 0) {
        branch_color = &theme->colors.git_dirty;
    } else {
        branch_color = &theme->colors.git_clean;
    }
    
    char color_code[LLE_COLOR_CODE_MAX];
    lle_color_to_ansi(branch_color, color_code, sizeof(color_code));
    
    pos += snprintf(status_buf + pos, sizeof(status_buf) - pos,
                    "%s%s%s", 
                    color_code,
                    theme->symbols.branch_symbol,
                    state->branch);
    
    // Status indicators
    if (state->staged_count > 0) {
        lle_color_to_ansi(&theme->colors.git_staged, color_code, sizeof(color_code));
        pos += snprintf(status_buf + pos, sizeof(status_buf) - pos,
                        " %s%s%d", color_code, theme->symbols.staged_symbol,
                        state->staged_count);
    }
    
    if (state->unstaged_count > 0) {
        lle_color_to_ansi(&theme->colors.git_dirty, color_code, sizeof(color_code));
        pos += snprintf(status_buf + pos, sizeof(status_buf) - pos,
                        " %s%s%d", color_code, theme->symbols.unstaged_symbol,
                        state->unstaged_count);
    }
    
    if (state->untracked_count > 0) {
        lle_color_to_ansi(&theme->colors.git_untracked, color_code, sizeof(color_code));
        pos += snprintf(status_buf + pos, sizeof(status_buf) - pos,
                        " %s%s%d", color_code, theme->symbols.untracked_symbol,
                        state->untracked_count);
    }
    
    // Ahead/behind
    if (state->ahead_count > 0) {
        lle_color_to_ansi(&theme->colors.git_ahead, color_code, sizeof(color_code));
        pos += snprintf(status_buf + pos, sizeof(status_buf) - pos,
                        " %s%s%d", color_code, theme->symbols.ahead_symbol,
                        state->ahead_count);
    }
    
    if (state->behind_count > 0) {
        lle_color_to_ansi(&theme->colors.git_behind, color_code, sizeof(color_code));
        pos += snprintf(status_buf + pos, sizeof(status_buf) - pos,
                        " %s%s%d", color_code, theme->symbols.behind_symbol,
                        state->behind_count);
    }
    
    // Reset color
    pos += snprintf(status_buf + pos, sizeof(status_buf) - pos, "\033[0m");
    
    snprintf(output->content, sizeof(output->content), "(%s)", status_buf);
    output->content_len = strlen(output->content);
    output->is_empty = false;
    output->needs_separator = true;
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Cache invalidation - called on CHPWD event
 */
static void segment_git_invalidate_cache(lle_prompt_segment_t *self) {
    lle_segment_git_state_t *state = self->state;
    state->cache_valid = false;
    state->is_git_repo = false;
}

/**
 * Property access for templates
 */
static const char *segment_git_get_property(
    const lle_prompt_segment_t *self,
    const char *prop
) {
    lle_segment_git_state_t *state = self->state;
    
    if (strcmp(prop, "branch") == 0) return state->branch;
    if (strcmp(prop, "remote") == 0) return state->remote_branch;
    if (strcmp(prop, "action") == 0) return state->action;
    
    return NULL;
}

/**
 * Git segment definition
 */
static lle_prompt_segment_t segment_git = {
    .name = "git",
    .description = "Git repository status",
    .capabilities = LLE_SEG_CAP_ASYNC | LLE_SEG_CAP_CACHEABLE | 
                    LLE_SEG_CAP_EXPENSIVE | LLE_SEG_CAP_THEME_AWARE |
                    LLE_SEG_CAP_OPTIONAL | LLE_SEG_CAP_PROPERTIES,
    .init = segment_git_init,
    .cleanup = segment_git_cleanup,
    .is_enabled = NULL,
    .is_visible = segment_git_is_visible,
    .render = segment_git_render,
    .get_property = segment_git_get_property,
    .request_async_data = segment_git_request_async,
    .on_async_data_ready = segment_git_on_data_ready,
    .is_cache_valid = NULL,
    .invalidate_cache = segment_git_invalidate_cache,
    .state = NULL
};
```

---

## 6. Template Engine

### 6.1 Template Syntax

The template engine supports the following syntax for prompt format strings:

```
Syntax Elements:
----------------
${segment}              - Render segment (e.g., ${directory}, ${git})
${segment.property}     - Access segment property (e.g., ${git.branch})
${?segment:true:false}  - Conditional: if segment visible, show true, else false
${?segment.prop:t:f}    - Conditional on property existence
${color:text}           - Apply theme color to text (e.g., ${primary:hello})
\n                      - Literal newline
\\                      - Escaped backslash
\$                      - Escaped dollar sign

Examples:
---------
${directory}${git} ${symbol}     - Simple prompt
${user}@${host}:${directory}$    - Traditional bash-style
${?git:${git} :}${symbol}        - Show git only in repos
╭─[${directory}]${?git: ${git}:}
╰─${symbol}                       - Multi-line with conditional
```

### 6.2 Template Parser Types

```c
/**
 * Template token types
 */
typedef enum lle_template_token_type {
    LLE_TOKEN_LITERAL,           // Plain text
    LLE_TOKEN_SEGMENT,           // ${segment}
    LLE_TOKEN_PROPERTY,          // ${segment.property}
    LLE_TOKEN_CONDITIONAL,       // ${?condition:true:false}
    LLE_TOKEN_COLOR,             // ${color:text}
    LLE_TOKEN_NEWLINE,           // \n
    LLE_TOKEN_END                // End of template
} lle_template_token_type_t;

/**
 * Parsed template token
 */
typedef struct lle_template_token {
    lle_template_token_type_t type;
    
    union {
        // Literal text
        struct {
            char text[256];
            size_t length;
        } literal;
        
        // Segment reference
        struct {
            char segment_name[LLE_SEGMENT_NAME_MAX];
            char property_name[LLE_PROPERTY_NAME_MAX];
            bool has_property;
        } segment;
        
        // Conditional
        struct {
            char condition_segment[LLE_SEGMENT_NAME_MAX];
            char condition_property[LLE_PROPERTY_NAME_MAX];
            char true_value[256];
            char false_value[256];
            bool check_property;
        } conditional;
        
        // Color application
        struct {
            char color_name[LLE_SEGMENT_NAME_MAX];
            char text[256];
        } color;
    } data;
    
    struct lle_template_token *next;
} lle_template_token_t;

/**
 * Parsed template (linked list of tokens)
 */
typedef struct lle_parsed_template {
    lle_template_token_t *head;
    lle_template_token_t *tail;
    size_t token_count;
    char original[LLE_TEMPLATE_MAX];
    bool valid;
} lle_parsed_template_t;
```

### 6.3 Template Parser Implementation

```c
/**
 * Parse a template string into tokens
 */
lle_prompt_result_t lle_template_parse(
    const char *template_str,
    lle_parsed_template_t **parsed
) {
    if (!template_str || !parsed) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    lle_parsed_template_t *tmpl = calloc(1, sizeof(*tmpl));
    if (!tmpl) {
        return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    }
    
    strncpy(tmpl->original, template_str, LLE_TEMPLATE_MAX - 1);
    
    const char *p = template_str;
    char literal_buf[256] = {0};
    size_t literal_pos = 0;
    
    while (*p) {
        // Check for escape sequences
        if (*p == '\\' && *(p + 1)) {
            if (*(p + 1) == 'n') {
                // Flush literal buffer first
                if (literal_pos > 0) {
                    lle_template_add_literal_token(tmpl, literal_buf, literal_pos);
                    literal_pos = 0;
                }
                lle_template_add_newline_token(tmpl);
                p += 2;
                continue;
            } else if (*(p + 1) == '$' || *(p + 1) == '\\') {
                literal_buf[literal_pos++] = *(p + 1);
                p += 2;
                continue;
            }
        }
        
        // Check for variable/segment reference
        if (*p == '$' && *(p + 1) == '{') {
            // Flush literal buffer
            if (literal_pos > 0) {
                lle_template_add_literal_token(tmpl, literal_buf, literal_pos);
                literal_pos = 0;
            }
            
            p += 2;  // Skip ${
            
            // Find closing brace
            const char *end = strchr(p, '}');
            if (!end) {
                free(tmpl);
                return LLE_PROMPT_ERROR_TEMPLATE_PARSE;
            }
            
            // Extract content between braces
            char content[256] = {0};
            size_t content_len = end - p;
            if (content_len >= sizeof(content)) {
                free(tmpl);
                return LLE_PROMPT_ERROR_TEMPLATE_PARSE;
            }
            strncpy(content, p, content_len);
            
            // Parse the content
            if (content[0] == '?') {
                // Conditional: ${?segment:true:false}
                lle_template_parse_conditional(tmpl, content + 1);
            } else if (strchr(content, ':') && !strchr(content, '.')) {
                // Color: ${color:text}
                lle_template_parse_color(tmpl, content);
            } else if (strchr(content, '.')) {
                // Property: ${segment.property}
                lle_template_parse_property(tmpl, content);
            } else {
                // Simple segment: ${segment}
                lle_template_parse_segment(tmpl, content);
            }
            
            p = end + 1;
            continue;
        }
        
        // Regular character
        if (literal_pos < sizeof(literal_buf) - 1) {
            literal_buf[literal_pos++] = *p;
        }
        p++;
    }
    
    // Flush remaining literal
    if (literal_pos > 0) {
        lle_template_add_literal_token(tmpl, literal_buf, literal_pos);
    }
    
    // Add end token
    lle_template_add_end_token(tmpl);
    
    tmpl->valid = true;
    *parsed = tmpl;
    
    return LLE_PROMPT_SUCCESS;
}
```

### 6.4 Template Renderer

```c
/**
 * Render a parsed template to output string
 */
lle_prompt_result_t lle_template_render(
    const lle_parsed_template_t *tmpl,
    const lle_prompt_context_t *ctx,
    const lle_theme_t *theme,
    lle_segment_registry_t *segments,
    char *output,
    size_t output_size
) {
    if (!tmpl || !ctx || !theme || !output || output_size == 0) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    if (!tmpl->valid) {
        return LLE_PROMPT_ERROR_TEMPLATE_PARSE;
    }
    
    size_t pos = 0;
    lle_template_token_t *token = tmpl->head;
    
    while (token && token->type != LLE_TOKEN_END && pos < output_size - 1) {
        switch (token->type) {
            case LLE_TOKEN_LITERAL:
                pos += snprintf(output + pos, output_size - pos, "%s",
                               token->data.literal.text);
                break;
                
            case LLE_TOKEN_NEWLINE:
                if (pos < output_size - 1) {
                    output[pos++] = '\n';
                }
                break;
                
            case LLE_TOKEN_SEGMENT: {
                lle_prompt_segment_t *seg = lle_segment_registry_find(
                    segments, token->data.segment.segment_name
                );
                if (seg && seg->is_visible && seg->is_visible(seg, ctx)) {
                    lle_segment_output_t seg_out = {0};
                    if (seg->render(seg, ctx, theme, &seg_out) == LLE_PROMPT_SUCCESS) {
                        if (!seg_out.is_empty) {
                            pos += snprintf(output + pos, output_size - pos,
                                           "%s", seg_out.content);
                        }
                    }
                }
                break;
            }
            
            case LLE_TOKEN_PROPERTY: {
                lle_prompt_segment_t *seg = lle_segment_registry_find(
                    segments, token->data.segment.segment_name
                );
                if (seg && seg->get_property) {
                    const char *value = seg->get_property(
                        seg, token->data.segment.property_name
                    );
                    if (value) {
                        pos += snprintf(output + pos, output_size - pos,
                                       "%s", value);
                    }
                }
                break;
            }
            
            case LLE_TOKEN_CONDITIONAL: {
                bool condition_met = false;
                lle_prompt_segment_t *seg = lle_segment_registry_find(
                    segments, token->data.conditional.condition_segment
                );
                
                if (seg) {
                    if (token->data.conditional.check_property) {
                        // Check property existence
                        if (seg->get_property) {
                            const char *val = seg->get_property(
                                seg, token->data.conditional.condition_property
                            );
                            condition_met = (val != NULL && strlen(val) > 0);
                        }
                    } else {
                        // Check segment visibility
                        condition_met = (!seg->is_visible || 
                                        seg->is_visible(seg, ctx));
                    }
                }
                
                const char *result = condition_met ?
                    token->data.conditional.true_value :
                    token->data.conditional.false_value;
                
                pos += snprintf(output + pos, output_size - pos, "%s", result);
                break;
            }
            
            case LLE_TOKEN_COLOR: {
                const lle_color_t *color = lle_theme_get_color_by_name(
                    theme, token->data.color.color_name
                );
                if (color) {
                    char color_code[LLE_COLOR_CODE_MAX];
                    lle_color_to_ansi(color, color_code, sizeof(color_code));
                    pos += snprintf(output + pos, output_size - pos,
                                   "%s%s\033[0m",
                                   color_code, token->data.color.text);
                } else {
                    pos += snprintf(output + pos, output_size - pos,
                                   "%s", token->data.color.text);
                }
                break;
            }
            
            default:
                break;
        }
        
        token = token->next;
    }
    
    output[pos] = '\0';
    return LLE_PROMPT_SUCCESS;
}
```

---

## 7. Async Data Providers

### 7.1 Async System Architecture

The async system uses a dedicated worker thread to process expensive operations (like git status) without blocking the main thread. This ensures prompt responsiveness even in large repositories.

```c
/**
 * Async request types
 */
typedef enum lle_async_request_type {
    LLE_ASYNC_REQUEST_GIT_STATUS,
    LLE_ASYNC_REQUEST_CUSTOM
} lle_async_request_type_t;

/**
 * Async request structure
 */
typedef struct lle_async_request {
    uint64_t id;                     // Unique request ID
    lle_async_request_type_t type;
    lle_prompt_segment_t *segment;   // Requesting segment
    char cwd[PATH_MAX];              // Working directory
    uint32_t timeout_ms;             // Timeout in milliseconds
    void *user_data;                 // Custom data
    
    struct lle_async_request *next;  // Queue linkage
} lle_async_request_t;

/**
 * Git status data returned from async worker
 */
typedef struct lle_git_status_data {
    char branch[256];
    char remote_branch[256];
    int staged_count;
    int unstaged_count;
    int untracked_count;
    int stash_count;
    int ahead_count;
    int behind_count;
    bool has_conflicts;
    char action[32];
    bool is_git_repo;
} lle_git_status_data_t;

/**
 * Async response structure
 */
typedef struct lle_async_response {
    uint64_t id;                     // Matching request ID
    lle_prompt_result_t result;      // Success or error
    
    union {
        lle_git_status_data_t git_status;
        void *custom_data;
    } data;
} lle_async_response_t;
```

### 7.2 Async Worker Thread

```c
/**
 * Async worker state
 */
typedef struct lle_async_worker {
    pthread_t thread;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    
    // Request queue
    lle_async_request_t *queue_head;
    lle_async_request_t *queue_tail;
    size_t queue_size;
    
    // State
    bool running;
    bool shutdown_requested;
    
    // Callback for completed requests
    void (*on_complete)(const lle_async_response_t *response, void *user_data);
    void *callback_user_data;
    
    // Statistics
    uint64_t total_requests;
    uint64_t total_completed;
    uint64_t total_timeouts;
    uint64_t next_request_id;
} lle_async_worker_t;

/**
 * Initialize async worker
 */
lle_prompt_result_t lle_async_worker_init(
    lle_async_worker_t **worker,
    void (*on_complete)(const lle_async_response_t *, void *),
    void *user_data
) {
    if (!worker) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    lle_async_worker_t *w = calloc(1, sizeof(*w));
    if (!w) {
        return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    }
    
    pthread_mutex_init(&w->queue_mutex, NULL);
    pthread_cond_init(&w->queue_cond, NULL);
    
    w->on_complete = on_complete;
    w->callback_user_data = user_data;
    w->running = false;
    w->shutdown_requested = false;
    w->next_request_id = 1;
    
    *worker = w;
    return LLE_PROMPT_SUCCESS;
}

/**
 * Start async worker thread
 */
lle_prompt_result_t lle_async_worker_start(lle_async_worker_t *worker) {
    if (!worker || worker->running) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    worker->running = true;
    
    if (pthread_create(&worker->thread, NULL, lle_async_worker_thread, worker) != 0) {
        worker->running = false;
        return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    }
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Worker thread main function
 */
static void *lle_async_worker_thread(void *arg) {
    lle_async_worker_t *worker = arg;
    
    while (!worker->shutdown_requested) {
        lle_async_request_t *request = NULL;
        
        // Wait for work
        pthread_mutex_lock(&worker->queue_mutex);
        while (worker->queue_head == NULL && !worker->shutdown_requested) {
            pthread_cond_wait(&worker->queue_cond, &worker->queue_mutex);
        }
        
        if (worker->shutdown_requested) {
            pthread_mutex_unlock(&worker->queue_mutex);
            break;
        }
        
        // Dequeue request
        request = worker->queue_head;
        worker->queue_head = request->next;
        if (worker->queue_head == NULL) {
            worker->queue_tail = NULL;
        }
        worker->queue_size--;
        pthread_mutex_unlock(&worker->queue_mutex);
        
        // Process request
        lle_async_response_t response = {0};
        response.id = request->id;
        
        switch (request->type) {
            case LLE_ASYNC_REQUEST_GIT_STATUS:
                response.result = lle_async_get_git_status(
                    request->cwd,
                    request->timeout_ms,
                    &response.data.git_status
                );
                break;
                
            default:
                response.result = LLE_PROMPT_ERROR_INVALID_PARAM;
                break;
        }
        
        // Notify completion
        if (worker->on_complete) {
            worker->on_complete(&response, worker->callback_user_data);
        }
        
        worker->total_completed++;
        free(request);
    }
    
    return NULL;
}

/**
 * Submit async request
 */
lle_prompt_result_t lle_async_worker_submit(
    lle_async_worker_t *worker,
    lle_async_request_t *request
) {
    if (!worker || !request || !worker->running) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    // Assign request ID
    request->id = worker->next_request_id++;
    request->next = NULL;
    
    pthread_mutex_lock(&worker->queue_mutex);
    
    // Enqueue
    if (worker->queue_tail) {
        worker->queue_tail->next = request;
    } else {
        worker->queue_head = request;
    }
    worker->queue_tail = request;
    worker->queue_size++;
    worker->total_requests++;
    
    pthread_cond_signal(&worker->queue_cond);
    pthread_mutex_unlock(&worker->queue_mutex);
    
    return LLE_PROMPT_SUCCESS;
}
```

### 7.3 Git Status Provider

All git commands run from the worker thread use `git -C <dir>` instead of
`chdir()` (which is process-wide and races with the main thread). Each command
has a wall-clock timeout enforced via `fork()/exec()/pipe()/select()/waitpid()`
to prevent indefinite hangs on slow filesystems or unreachable remotes.

See `include/lle/git_command.h` for the timed command execution API and
`src/lle/core/async_worker.c` for the git status implementation.

```c
/**
 * Get git status (runs in worker thread)
 *
 * Uses git -C <cwd> for all commands (no process-wide chdir).
 * All commands respect the timeout_ms parameter via
 * git_command_in_dir() which uses fork/exec/pipe/select/waitpid.
 */
static lle_result_t lle_async_get_git_status(
    const char *cwd,
    uint32_t timeout_ms,
    lle_git_status_data_t *status
) {
    if (!cwd || !status) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (timeout_ms == 0) {
        timeout_ms = GIT_CMD_ASYNC_TIMEOUT_MS;  /* 5000ms default */
    }

    memset(status, 0, sizeof(*status));

    /* Check if in git repo (with timeout) */
    if (!run_git_in_dir(cwd, "rev-parse --git-dir", NULL, 0, timeout_ms)) {
        status->is_git_repo = false;
        return LLE_SUCCESS;
    }
    status->is_git_repo = true;

    /* Get branch name */
    run_git_in_dir(cwd, "branch --show-current",
                   status->branch, sizeof(status->branch), timeout_ms);

    /* Get status counts from single porcelain call */
    char porcelain[8192] = {0};
    if (run_git_in_dir(cwd, "status --porcelain",
                       porcelain, sizeof(porcelain), timeout_ms)) {
        /* Parse porcelain output line by line */
        /* ... count staged, unstaged, untracked ... */
    }

    /* Get ahead/behind counts */
    char ahead_behind[64] = {0};
    if (run_git_in_dir(cwd, "rev-list --left-right --count HEAD...@{upstream}",
                       ahead_behind, sizeof(ahead_behind), timeout_ms)) {
        sscanf(ahead_behind, "%d %d", &status->ahead, &status->behind);
    }

    /* Check for merge/rebase using access() on git-dir paths */
    /* ... */

    return LLE_SUCCESS;
}
```

---

## 8. Event Integration

The prompt system integrates with the LLE shell event hub (Spec 26) for automatic
cache invalidation and prompt regeneration. This is the core solution to Issue #16
(stale git prompt).

**IMPORTANT**: The prompt system does NOT define its own event types. It registers
handlers with the shell event hub defined in Spec 26 (LLE Initialization System).

### 8.1 Shell Event Hub Integration

The prompt composer registers handlers for shell events through `lle_shell_event_hub_t`
(defined in Spec 26). This ensures a single, unified event system across LLE.

```c
/**
 * @brief Shell event types (from Spec 26)
 *
 * These are defined in lle_shell_event_hub.h, NOT here.
 * Shown for reference only.
 */
typedef enum lle_shell_event_type {
    LLE_SHELL_EVENT_DIRECTORY_CHANGED,  /**< cd/pushd/popd completed */
    LLE_SHELL_EVENT_PRE_COMMAND,        /**< About to execute command */
    LLE_SHELL_EVENT_POST_COMMAND,       /**< Command execution finished */
} lle_shell_event_type_t;

/**
 * @brief Prompt-specific internal events
 *
 * These are internal to the prompt system, fired after processing
 * shell events. User code should subscribe to these for prompt updates.
 */
typedef enum lle_prompt_event {
    LLE_PROMPT_EVENT_THEME_CHANGE,   /**< Theme was changed */
    LLE_PROMPT_EVENT_GIT_UPDATE,     /**< Git async data ready */
    LLE_PROMPT_EVENT_CACHE_EXPIRE,   /**< Cache entry invalidated */
    LLE_PROMPT_EVENT_REGENERATED,    /**< Prompt was regenerated */
} lle_prompt_event_t;

/**
 * @brief Prompt event callback signature
 *
 * @param event Event type
 * @param event_data Event-specific data (may be NULL)
 * @param user_data User-provided context
 */
typedef void (*lle_prompt_event_callback_t)(
    lle_prompt_event_t event,
    void *event_data,
    void *user_data
);
```

### 8.2 Directory Change Handler (Issue #16 Fix)

```c
/**
 * @brief Handler for LLE_SHELL_EVENT_DIRECTORY_CHANGED
 *
 * This is the core fix for Issue #16 (stale git prompt). When the
 * directory changes, we invalidate caches and queue async refresh.
 *
 * @param event_data Pointer to lle_directory_changed_event_t (from Spec 26)
 * @param user_data Pointer to lle_prompt_composer_t
 */
static void lle_prompt_on_directory_changed(void *event_data, void *user_data) {
    lle_prompt_composer_t *composer = user_data;
    lle_directory_changed_event_t *event = event_data;
    
    if (!composer) return;
    
    /* Invalidate git cache - this is the key fix for Issue #16 */
    /* Instead of time-based expiry, we invalidate on directory change */
    lle_prompt_cache_invalidate_by_tag(&composer->cache, "git");
    
    /* Also invalidate directory-specific caches */
    lle_prompt_cache_invalidate_by_tag(&composer->cache, "directory");
    lle_prompt_cache_invalidate_by_tag(&composer->cache, "vcs");
    
    /* Queue async git status refresh for new directory */
    if (composer->async_worker && composer->async_worker->running) {
        lle_async_request_t *request = calloc(1, sizeof(*request));
        if (request) {
            request->type = LLE_ASYNC_REQUEST_GIT_STATUS;
            strncpy(request->cwd, event->new_dir, PATH_MAX - 1);
            request->timeout_ms = 1000;
            lle_async_worker_submit(composer->async_worker, request);
        }
    }
    
    /* Mark prompt for regeneration */
    composer->needs_regeneration = true;
    
    /* Fire internal event for subscribers */
    lle_prompt_fire_event(composer, LLE_PROMPT_EVENT_CACHE_EXPIRE, NULL);
}
```

### 8.3 Pre-Command Handler

```c
/**
 * @brief Handler for LLE_SHELL_EVENT_PRE_COMMAND
 *
 * Called just before command execution begins. This is where we:
 * 1. Apply transient prompt to the current prompt line
 * 2. Record command start time for duration calculation
 *
 * @param event_data Pointer to lle_pre_command_event_t (from Spec 26)
 * @param user_data Pointer to lle_prompt_composer_t
 */
static void lle_prompt_on_pre_command(void *event_data, void *user_data) {
    lle_prompt_composer_t *composer = user_data;
    lle_pre_command_event_t *event = event_data;
    
    if (!composer) return;
    
    /* Apply transient prompt if enabled - simplify current prompt before
     * command output starts, reducing visual clutter */
    if (composer->current_theme && 
        composer->current_theme->layout.enable_transient &&
        composer->last_prompt_line > 0) {
        lle_prompt_apply_transient(composer);
    }
    
    /* Save command info for post-command handling */
    composer->current_command = event->command;
    composer->current_command_is_bg = event->is_background;
    
    /* Command start time is recorded in shell event hub (Spec 26) */
}
```

### 8.4 Post-Command Handler

```c
/**
 * @brief Handler for LLE_SHELL_EVENT_POST_COMMAND
 *
 * Called after command execution completes. This is where we:
 * 1. Update context with exit code and duration
 * 2. Mark prompt for regeneration
 *
 * @param event_data Pointer to lle_post_command_event_t (from Spec 26)
 * @param user_data Pointer to lle_prompt_composer_t
 */
static void lle_prompt_on_post_command(void *event_data, void *user_data) {
    lle_prompt_composer_t *composer = user_data;
    lle_post_command_event_t *event = event_data;
    
    if (!composer) return;
    
    /* Update context with command results */
    composer->context.last_exit_code = event->exit_code;
    composer->context.last_cmd_duration_ms = event->duration_us / 1000;
    
    /* Mark prompt for regeneration - exit code may affect display */
    composer->needs_regeneration = true;
    
    /* Fire internal event */
    lle_prompt_fire_event(composer, LLE_PROMPT_EVENT_REGENERATED, NULL);
}

/**
 * @brief Regenerate prompt from current theme and segments
 *
 * @param composer The prompt composer
 * @return LLE_PROMPT_SUCCESS or error code
 */
static lle_prompt_result_t lle_prompt_regenerate(lle_prompt_composer_t *composer) {
    if (!composer || !composer->current_theme) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    /* Free old cached prompt */
    free(composer->cached_prompt);
    composer->cached_prompt = NULL;
    
    /* Determine which template to use based on prompt type */
    const char *template = NULL;
    switch (composer->prompt_type) {
        case LLE_PROMPT_PS1:
            template = composer->current_theme->layout.left_format;
            break;
        case LLE_PROMPT_PS2:
            template = composer->current_theme->layout.continuation_format;
            break;
        case LLE_PROMPT_RPROMPT:
            template = composer->current_theme->layout.right_format;
            break;
        default:
            template = composer->current_theme->layout.left_format;
            break;
    }
    
    if (!template) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    /* Render template */
    lle_prompt_result_t result = lle_template_render(
        composer->parsed_template,
        &composer->context,
        composer->current_theme,
        composer->segment_registry,
        composer->cached_prompt,
        LLE_PROMPT_OUTPUT_MAX
    );
    
    if (result == LLE_PROMPT_SUCCESS) {
        composer->needs_regeneration = false;
    }
    
    return result;
}
```

### 8.5 Async Completion Handler

```c
/**
 * @brief Callback when async git status completes
 *
 * This runs in the main thread after async worker signals completion.
 *
 * @param response The async response with git status data
 * @param user_data Pointer to lle_prompt_composer_t
 */
static void lle_prompt_on_async_complete(
    lle_async_response_t *response,
    void *user_data
) {
    lle_prompt_composer_t *composer = user_data;
    
    if (!composer || !response) return;
    
    if (response->result == LLE_PROMPT_SUCCESS) {
        /* Cache the git status */
        lle_prompt_cache_entry_t *entry = calloc(1, sizeof(*entry));
        if (entry) {
            entry->key = strdup("git_status");
            entry->data = malloc(sizeof(lle_git_status_data_t));
            if (entry->data) {
                memcpy(entry->data, &response->data.git_status, 
                       sizeof(lle_git_status_data_t));
                entry->data_size = sizeof(lle_git_status_data_t);
                entry->timestamp = time(NULL);
                entry->ttl = UINT32_MAX;  /* Never expire by time - only by event */
                
                /* Add tag for event-based invalidation */
                entry->tags = malloc(sizeof(char*));
                if (entry->tags) {
                    entry->tags[0] = strdup("git");
                    entry->tag_count = 1;
                }
                
                lle_prompt_cache_set(&composer->cache, entry);
            }
        }
        
        /* Fire internal update event */
        lle_prompt_fire_event(composer, LLE_PROMPT_EVENT_GIT_UPDATE, 
                              &response->data.git_status);
        
        /* Mark for regeneration - new git data available */
        composer->needs_regeneration = true;
    }
}
```

### 8.6 Shell Event Hub Registration

This is the critical integration point with Spec 26. The prompt composer registers
its handlers with the shell event hub during initialization.

```c
/**
 * @brief Register prompt handlers with shell event hub
 *
 * Called during lle_prompt_composer_init() to wire up event handlers.
 * The shell event hub is obtained from the LLE shell integration (Spec 26).
 *
 * @param composer The prompt composer to register
 * @param event_hub The shell event hub from Spec 26
 * @return LLE_PROMPT_SUCCESS or error code
 */
lle_prompt_result_t lle_prompt_register_shell_events(
    lle_prompt_composer_t *composer,
    lle_shell_event_hub_t *event_hub
) {
    if (!composer || !event_hub) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    lle_result_t result;
    
    /* Register directory change handler (Issue #16 fix) */
    result = lle_shell_event_hub_register(
        event_hub,
        LLE_SHELL_EVENT_DIRECTORY_CHANGED,
        lle_prompt_on_directory_changed,
        composer
    );
    if (result != LLE_SUCCESS) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    /* Register pre-command handler (transient prompt) */
    result = lle_shell_event_hub_register(
        event_hub,
        LLE_SHELL_EVENT_PRE_COMMAND,
        lle_prompt_on_pre_command,
        composer
    );
    if (result != LLE_SUCCESS) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    /* Register post-command handler (exit code, duration) */
    result = lle_shell_event_hub_register(
        event_hub,
        LLE_SHELL_EVENT_POST_COMMAND,
        lle_prompt_on_post_command,
        composer
    );
    if (result != LLE_SUCCESS) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    /* Store reference to event hub for later use */
    composer->shell_event_hub = event_hub;
    
    return LLE_PROMPT_SUCCESS;
}
```

### 8.7 Internal Event Firing

The prompt system has its own internal events for subscribers who want to react
to prompt-specific changes (theme changes, git updates, etc.).

```c
/** @brief Maximum internal event subscribers */
#define LLE_MAX_PROMPT_EVENT_SUBSCRIBERS 32

/**
 * @brief Internal event subscription
 */
typedef struct {
    lle_prompt_event_t event;
    lle_prompt_event_callback_t callback;
    void *user_data;
    int priority;
} lle_prompt_event_subscription_t;

/**
 * @brief Internal event manager
 */
typedef struct {
    lle_prompt_event_subscription_t subscribers[LLE_MAX_PROMPT_EVENT_SUBSCRIBERS];
    size_t count;
    pthread_mutex_t lock;
} lle_prompt_event_manager_t;

/**
 * @brief Subscribe to internal prompt events
 *
 * @param composer The prompt composer
 * @param event Event type to subscribe to
 * @param callback Callback function
 * @param user_data User-provided context
 * @param priority Higher priority callbacks run first
 * @return LLE_PROMPT_SUCCESS or error code
 */
lle_prompt_result_t lle_prompt_event_subscribe(
    lle_prompt_composer_t *composer,
    lle_prompt_event_t event,
    lle_prompt_event_callback_t callback,
    void *user_data,
    int priority
) {
    if (!composer || !callback) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    lle_prompt_event_manager_t *mgr = &composer->event_manager;
    
    pthread_mutex_lock(&mgr->lock);
    
    if (mgr->count >= LLE_MAX_PROMPT_EVENT_SUBSCRIBERS) {
        pthread_mutex_unlock(&mgr->lock);
        return LLE_PROMPT_ERROR_REGISTRY_FULL;
    }
    
    /* Insert sorted by priority (higher first) */
    size_t insert_pos = mgr->count;
    for (size_t i = 0; i < mgr->count; i++) {
        if (mgr->subscribers[i].priority < priority) {
            insert_pos = i;
            break;
        }
    }
    
    /* Shift existing entries */
    if (insert_pos < mgr->count) {
        memmove(&mgr->subscribers[insert_pos + 1],
                &mgr->subscribers[insert_pos],
                (mgr->count - insert_pos) * sizeof(lle_prompt_event_subscription_t));
    }
    
    /* Insert new subscription */
    mgr->subscribers[insert_pos] = (lle_prompt_event_subscription_t){
        .event = event,
        .callback = callback,
        .user_data = user_data,
        .priority = priority,
    };
    mgr->count++;
    
    pthread_mutex_unlock(&mgr->lock);
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * @brief Fire an internal event to all subscribers
 *
 * @param composer The prompt composer
 * @param event Event type
 * @param event_data Event-specific data (may be NULL)
 */
void lle_prompt_fire_event(
    lle_prompt_composer_t *composer,
    lle_prompt_event_t event,
    void *event_data
) {
    if (!composer) return;
    
    lle_prompt_event_manager_t *mgr = &composer->event_manager;
    
    pthread_mutex_lock(&mgr->lock);
    
    for (size_t i = 0; i < mgr->count; i++) {
        if (mgr->subscribers[i].event == event) {
            /* Unlock during callback to prevent deadlock */
            lle_prompt_event_callback_t cb = mgr->subscribers[i].callback;
            void *user_data = mgr->subscribers[i].user_data;
            pthread_mutex_unlock(&mgr->lock);
            
            cb(event, event_data, user_data);
            
            pthread_mutex_lock(&mgr->lock);
        }
    }
    
    pthread_mutex_unlock(&mgr->lock);
}
```

---

## 9. Display Integration

The prompt system integrates with LLE's screen_buffer for efficient rendering.
This provides differential updates and proper handling of terminal capabilities.

### 9.1 Screen Buffer Integration

```c
/**
 * Prompt rendering context for screen_buffer integration
 */
typedef struct {
    lle_screen_buffer_t *screen;       // Main screen buffer
    lle_prompt_context_t *prompt_ctx;  // Prompt context
    
    // Prompt line tracking
    int ps1_start_line;                // Line where PS1 starts
    int ps1_end_line;                  // Line where PS1 ends
    int rprompt_line;                  // Line for right prompt
    int rprompt_col;                   // Column for right prompt
    
    // Continuation state
    int continuation_count;            // Number of continuation prompts shown
    bool in_continuation;              // Currently in multi-line input
} lle_prompt_display_t;

/**
 * Initialize prompt display integration
 */
lle_prompt_result_t lle_prompt_display_init(
    lle_prompt_display_t *display,
    lle_screen_buffer_t *screen,
    lle_prompt_context_t *prompt_ctx
) {
    if (!display || !screen || !prompt_ctx) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    memset(display, 0, sizeof(*display));
    display->screen = screen;
    display->prompt_ctx = prompt_ctx;
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Render prompt to screen buffer
 * 
 * This is the main entry point for prompt display. It:
 * 1. Parses the prompt string for ANSI sequences
 * 2. Writes styled cells to the screen buffer
 * 3. Tracks prompt boundaries for transient prompt
 */
lle_prompt_result_t lle_prompt_display_render(
    lle_prompt_display_t *display,
    lle_prompt_type_t type
) {
    if (!display || !display->screen || !display->prompt_ctx) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    const char *prompt_str = NULL;
    
    // Get the rendered prompt string
    switch (type) {
        case LLE_PROMPT_PS1:
            prompt_str = display->prompt_ctx->cached_prompt;
            display->ps1_start_line = display->screen->cursor_y;
            break;
        case LLE_PROMPT_PS2:
            prompt_str = display->prompt_ctx->cached_ps2;
            display->continuation_count++;
            break;
        case LLE_PROMPT_RPROMPT:
            prompt_str = display->prompt_ctx->cached_rprompt;
            break;
        default:
            return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    if (!prompt_str) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    // For right prompt, calculate position
    if (type == LLE_PROMPT_RPROMPT) {
        size_t visible_len = lle_prompt_visible_length(prompt_str);
        display->rprompt_line = display->screen->cursor_y;
        display->rprompt_col = display->screen->width - visible_len;
        
        // Position cursor for right prompt
        lle_screen_buffer_move_cursor(display->screen, 
                                       display->rprompt_col, 
                                       display->rprompt_line);
    }
    
    // Parse and render the prompt string
    lle_prompt_result_t result = lle_prompt_display_write_styled(
        display,
        prompt_str
    );
    
    if (type == LLE_PROMPT_PS1) {
        display->ps1_end_line = display->screen->cursor_y;
    }
    
    return result;
}
```

### 9.2 Styled Text Rendering

```c
/**
 * ANSI escape sequence parser state
 */
typedef struct {
    bool in_escape;
    bool in_csi;
    char params[64];
    size_t param_len;
    lle_cell_style_t current_style;
} lle_ansi_parser_t;

/**
 * Write styled text to screen buffer
 * Parses ANSI sequences and converts to cell styles
 */
static lle_prompt_result_t lle_prompt_display_write_styled(
    lle_prompt_display_t *display,
    const char *str
) {
    lle_ansi_parser_t parser = {0};
    parser.current_style = display->screen->current_style;
    
    const char *p = str;
    while (*p) {
        if (*p == '\033') {
            // Start of escape sequence
            parser.in_escape = true;
            parser.param_len = 0;
            p++;
            continue;
        }
        
        if (parser.in_escape) {
            if (*p == '[') {
                parser.in_csi = true;
                p++;
                continue;
            }
            
            if (parser.in_csi) {
                if (*p >= '0' && *p <= '9' || *p == ';') {
                    // Accumulate parameters
                    if (parser.param_len < sizeof(parser.params) - 1) {
                        parser.params[parser.param_len++] = *p;
                    }
                    p++;
                    continue;
                }
                
                // End of CSI sequence
                parser.params[parser.param_len] = '\0';
                
                if (*p == 'm') {
                    // SGR (Select Graphic Rendition)
                    lle_prompt_display_parse_sgr(&parser);
                    display->screen->current_style = parser.current_style;
                }
                
                parser.in_escape = false;
                parser.in_csi = false;
                p++;
                continue;
            }
            
            // Non-CSI escape - skip
            parser.in_escape = false;
            p++;
            continue;
        }
        
        // Regular character - write to buffer
        if (*p == '\n') {
            lle_screen_buffer_newline(display->screen);
        } else if (*p == '\r') {
            display->screen->cursor_x = 0;
        } else {
            // Handle UTF-8 multi-byte sequences
            uint32_t codepoint;
            int bytes = lle_utf8_decode(p, &codepoint);
            if (bytes > 0) {
                lle_screen_buffer_write_cell(display->screen, codepoint);
                p += bytes - 1;  // -1 because loop will increment
            }
        }
        
        p++;
    }
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Parse SGR (color/style) parameters
 */
static void lle_prompt_display_parse_sgr(lle_ansi_parser_t *parser) {
    char *saveptr;
    char *token = strtok_r(parser->params, ";", &saveptr);
    
    while (token) {
        int code = atoi(token);
        
        switch (code) {
            case 0:  // Reset
                parser->current_style = (lle_cell_style_t){0};
                break;
            case 1:  // Bold
                parser->current_style.bold = true;
                break;
            case 2:  // Dim
                parser->current_style.dim = true;
                break;
            case 3:  // Italic
                parser->current_style.italic = true;
                break;
            case 4:  // Underline
                parser->current_style.underline = true;
                break;
            case 7:  // Reverse
                parser->current_style.reverse = true;
                break;
            case 22: // Normal intensity
                parser->current_style.bold = false;
                parser->current_style.dim = false;
                break;
            case 23: // Not italic
                parser->current_style.italic = false;
                break;
            case 24: // Not underline
                parser->current_style.underline = false;
                break;
            case 27: // Not reverse
                parser->current_style.reverse = false;
                break;
            case 30 ... 37:  // Standard foreground colors
                parser->current_style.fg = code - 30;
                break;
            case 38:  // Extended foreground color
                // Check for 256-color or RGB
                token = strtok_r(NULL, ";", &saveptr);
                if (token && atoi(token) == 5) {
                    // 256-color mode
                    token = strtok_r(NULL, ";", &saveptr);
                    if (token) {
                        parser->current_style.fg = atoi(token);
                    }
                }
                break;
            case 39:  // Default foreground
                parser->current_style.fg = -1;
                break;
            case 40 ... 47:  // Standard background colors
                parser->current_style.bg = code - 40;
                break;
            case 48:  // Extended background color
                token = strtok_r(NULL, ";", &saveptr);
                if (token && atoi(token) == 5) {
                    token = strtok_r(NULL, ";", &saveptr);
                    if (token) {
                        parser->current_style.bg = atoi(token);
                    }
                }
                break;
            case 49:  // Default background
                parser->current_style.bg = -1;
                break;
            case 90 ... 97:  // Bright foreground colors
                parser->current_style.fg = code - 90 + 8;
                break;
            case 100 ... 107:  // Bright background colors
                parser->current_style.bg = code - 100 + 8;
                break;
        }
        
        token = strtok_r(NULL, ";", &saveptr);
    }
}
```

### 9.3 Visible Length Calculation

```c
/**
 * Calculate visible length of prompt string (excluding ANSI sequences)
 * 
 * This is critical for:
 * - Right prompt positioning
 * - Line wrap calculation
 * - Cursor positioning
 */
size_t lle_prompt_visible_length(const char *str) {
    if (!str) return 0;
    
    size_t len = 0;
    bool in_escape = false;
    
    const char *p = str;
    while (*p) {
        if (*p == '\033') {
            in_escape = true;
            p++;
            continue;
        }
        
        if (in_escape) {
            if (*p == 'm' || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
                in_escape = false;
            }
            p++;
            continue;
        }
        
        // Count visible character
        // Handle UTF-8 multi-byte sequences
        if ((*p & 0x80) == 0) {
            // ASCII
            if (*p >= 0x20) {  // Printable
                len++;
            }
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            len++;  // 2-byte UTF-8
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            len++;  // 3-byte UTF-8
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            len += 2;  // 4-byte UTF-8 (wide char)
            p += 4;
        } else {
            p++;
        }
    }
    
    return len;
}

/**
 * Calculate display width of string (accounting for wide characters)
 */
size_t lle_prompt_display_width(const char *str) {
    if (!str) return 0;
    
    size_t width = 0;
    bool in_escape = false;
    
    const char *p = str;
    while (*p) {
        if (*p == '\033') {
            in_escape = true;
            p++;
            continue;
        }
        
        if (in_escape) {
            if (*p == 'm' || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
                in_escape = false;
            }
            p++;
            continue;
        }
        
        // Get character width
        uint32_t codepoint;
        int bytes = lle_utf8_decode(p, &codepoint);
        if (bytes > 0) {
            width += lle_wcwidth(codepoint);
            p += bytes;
        } else {
            p++;
        }
    }
    
    return width;
}
```

### 9.4 Flush to Terminal

```c
/**
 * Flush prompt display to terminal
 * Uses screen_buffer differential update for efficiency
 */
lle_prompt_result_t lle_prompt_display_flush(lle_prompt_display_t *display) {
    if (!display || !display->screen) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    // Use screen_buffer's differential flush
    lle_screen_buffer_flush(display->screen);
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Force full redraw of prompt
 * Used after terminal resize or corruption
 */
lle_prompt_result_t lle_prompt_display_redraw(lle_prompt_display_t *display) {
    if (!display || !display->screen || !display->prompt_ctx) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    // Clear the prompt area
    for (int y = display->ps1_start_line; y <= display->ps1_end_line; y++) {
        lle_screen_buffer_clear_line(display->screen, y);
    }
    
    // Re-render
    lle_screen_buffer_move_cursor(display->screen, 0, display->ps1_start_line);
    lle_prompt_display_render(display, LLE_PROMPT_PS1);
    
    if (display->prompt_ctx->cached_rprompt) {
        lle_prompt_display_render(display, LLE_PROMPT_RPROMPT);
    }
    
    lle_prompt_display_flush(display);
    
    return LLE_PROMPT_SUCCESS;
}
```

---

## 10. Configuration System

The configuration system provides a layered approach to prompt customization,
from TOML files to shell variables to runtime API.

### 10.1 Configuration Hierarchy

```
Priority (highest to lowest):
1. Runtime API calls (lle_prompt_set_*)
2. Shell variable: $LUSH_PROMPT
3. User config: ~/.config/lush/prompt.toml
4. System config: /etc/lush/prompt.toml
5. Built-in defaults
```

### 10.2 Configuration Structure

```c
/**
 * Configuration source tracking
 */
typedef enum {
    LLE_CONFIG_SOURCE_BUILTIN,
    LLE_CONFIG_SOURCE_SYSTEM,
    LLE_CONFIG_SOURCE_USER,
    LLE_CONFIG_SOURCE_SHELL_VAR,
    LLE_CONFIG_SOURCE_RUNTIME,
} lle_config_source_t;

/**
 * Complete prompt configuration
 */
typedef struct {
    // Active theme
    char theme_name[64];
    lle_config_source_t theme_source;
    
    // Shell variable overrides
    char *ps1_override;       // From $PS1 or $LUSH_PS1
    char *ps2_override;       // From $PS2 or $LUSH_PS2
    char *rprompt_override;   // From $RPROMPT or $LUSH_RPROMPT
    
    // Async configuration
    struct {
        bool enabled;
        uint32_t git_timeout_ms;
        uint32_t max_queue_size;
    } async;
    
    // Transient prompt configuration
    struct {
        bool enabled;
        char *template;
    } transient;
    
    // Cache configuration
    struct {
        uint32_t max_entries;
        uint32_t default_ttl;
    } cache;
    
    // Symbol compatibility
    lle_symbol_mode_t symbol_mode;
    
    // Color mode
    lle_color_mode_t color_mode;
    
    // Loaded config files
    char *user_config_path;
    char *system_config_path;
} lle_prompt_config_t;

/**
 * Symbol compatibility modes
 */
typedef enum {
    LLE_SYMBOL_MODE_AUTO,     // Detect terminal capabilities
    LLE_SYMBOL_MODE_UNICODE,  // Full Unicode (Nerd Fonts, Powerline)
    LLE_SYMBOL_MODE_ASCII,    // ASCII-only fallback
    LLE_SYMBOL_MODE_MINIMAL,  // Minimal symbols
} lle_symbol_mode_t;

/**
 * Color modes
 */
typedef enum {
    LLE_COLOR_MODE_AUTO,      // Detect from $COLORTERM, terminfo
    LLE_COLOR_MODE_TRUECOLOR, // 24-bit RGB
    LLE_COLOR_MODE_256,       // 256 color palette
    LLE_COLOR_MODE_16,        // Basic 16 colors
    LLE_COLOR_MODE_NONE,      // No colors
} lle_color_mode_t;
```

### 10.3 TOML Configuration Format

```toml
# ~/.config/lush/prompt.toml
# Lush Prompt Configuration

# Theme selection
theme = "powerline"

# Async settings
[async]
enabled = true
git_timeout_ms = 1000
max_queue_size = 8

# Transient prompt (simplify previous prompts)
[transient]
enabled = true
template = "${symbol.prompt} "

# Cache settings
[cache]
max_entries = 64
default_ttl = 300

# Symbol mode: "auto", "unicode", "ascii", "minimal"
symbol_mode = "auto"

# Color mode: "auto", "truecolor", "256", "16", "none"
color_mode = "auto"

# Custom theme definition (inline)
[themes.my_custom_theme]
parent = "minimal"

[themes.my_custom_theme.templates]
ps1 = "${directory.short} ${git.branch} ${symbol.prompt} "
ps2 = "${symbol.continuation} "

[themes.my_custom_theme.colors]
directory = "blue"
git_branch = "green"
prompt_char = "cyan"

[themes.my_custom_theme.symbols]
prompt = "❯"
continuation = "…"

# Segment configuration
[segments.git]
show_status = true
show_ahead_behind = true
show_stash = false

[segments.directory]
max_depth = 3
truncation = "middle"  # "none", "left", "middle", "right"

[segments.time]
format = "%H:%M"
```

### 10.4 TOML Parser Integration

```c
/**
 * Parse TOML configuration file
 */
lle_prompt_result_t lle_prompt_config_load_toml(
    lle_prompt_config_t *config,
    const char *path
) {
    if (!config || !path) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    // Check file exists
    if (access(path, R_OK) != 0) {
        return LLE_PROMPT_ERROR_NOT_FOUND;
    }
    
    // Use toml.c or similar library
    // This is a simplified implementation
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return LLE_PROMPT_ERROR_NOT_FOUND;
    }
    
    char line[1024];
    char current_section[64] = "";
    
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        char *p = line;
        while (*p && isspace(*p)) p++;
        if (*p == '#' || *p == '\0' || *p == '\n') continue;
        
        // Section header
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, p + 1, sizeof(current_section) - 1);
            }
            continue;
        }
        
        // Key = value
        char *eq = strchr(p, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char *key = p;
        char *value = eq + 1;
        
        // Trim whitespace
        while (*key && isspace(*key)) key++;
        char *key_end = key + strlen(key) - 1;
        while (key_end > key && isspace(*key_end)) *key_end-- = '\0';
        
        while (*value && isspace(*value)) value++;
        // Remove quotes
        if (*value == '"') {
            value++;
            char *quote_end = strchr(value, '"');
            if (quote_end) *quote_end = '\0';
        }
        
        // Apply configuration based on section and key
        lle_prompt_config_apply_value(config, current_section, key, value);
    }
    
    fclose(fp);
    return LLE_PROMPT_SUCCESS;
}

/**
 * Apply a single configuration value
 */
static void lle_prompt_config_apply_value(
    lle_prompt_config_t *config,
    const char *section,
    const char *key,
    const char *value
) {
    if (strcmp(section, "") == 0) {
        // Root level
        if (strcmp(key, "theme") == 0) {
            strncpy(config->theme_name, value, sizeof(config->theme_name) - 1);
        } else if (strcmp(key, "symbol_mode") == 0) {
            if (strcmp(value, "unicode") == 0) {
                config->symbol_mode = LLE_SYMBOL_MODE_UNICODE;
            } else if (strcmp(value, "ascii") == 0) {
                config->symbol_mode = LLE_SYMBOL_MODE_ASCII;
            } else if (strcmp(value, "minimal") == 0) {
                config->symbol_mode = LLE_SYMBOL_MODE_MINIMAL;
            } else {
                config->symbol_mode = LLE_SYMBOL_MODE_AUTO;
            }
        } else if (strcmp(key, "color_mode") == 0) {
            if (strcmp(value, "truecolor") == 0) {
                config->color_mode = LLE_COLOR_MODE_TRUECOLOR;
            } else if (strcmp(value, "256") == 0) {
                config->color_mode = LLE_COLOR_MODE_256;
            } else if (strcmp(value, "16") == 0) {
                config->color_mode = LLE_COLOR_MODE_16;
            } else if (strcmp(value, "none") == 0) {
                config->color_mode = LLE_COLOR_MODE_NONE;
            } else {
                config->color_mode = LLE_COLOR_MODE_AUTO;
            }
        }
    } else if (strcmp(section, "async") == 0) {
        if (strcmp(key, "enabled") == 0) {
            config->async.enabled = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "git_timeout_ms") == 0) {
            config->async.git_timeout_ms = atoi(value);
        } else if (strcmp(key, "max_queue_size") == 0) {
            config->async.max_queue_size = atoi(value);
        }
    } else if (strcmp(section, "transient") == 0) {
        if (strcmp(key, "enabled") == 0) {
            config->transient.enabled = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "template") == 0) {
            free(config->transient.template);
            config->transient.template = strdup(value);
        }
    } else if (strcmp(section, "cache") == 0) {
        if (strcmp(key, "max_entries") == 0) {
            config->cache.max_entries = atoi(value);
        } else if (strcmp(key, "default_ttl") == 0) {
            config->cache.default_ttl = atoi(value);
        }
    }
    // Theme and segment sections handled by theme/segment loaders
}
```

### 10.5 Shell Variable Integration

```c
/**
 * Check and apply shell variable overrides
 */
lle_prompt_result_t lle_prompt_config_apply_shell_vars(
    lle_prompt_config_t *config
) {
    if (!config) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    // Check $LUSH_PROMPT for theme override
    const char *prompt_var = getenv("LUSH_PROMPT");
    if (prompt_var && *prompt_var) {
        // Could be a theme name or a template
        if (strchr(prompt_var, '$') || strchr(prompt_var, '%')) {
            // Contains template syntax - use as PS1 override
            free(config->ps1_override);
            config->ps1_override = strdup(prompt_var);
        } else {
            // Assume it's a theme name
            strncpy(config->theme_name, prompt_var, sizeof(config->theme_name) - 1);
            config->theme_source = LLE_CONFIG_SOURCE_SHELL_VAR;
        }
    }
    
    // Check $LUSH_PS1 (takes precedence over $PS1)
    const char *ps1_var = getenv("LUSH_PS1");
    if (!ps1_var) {
        ps1_var = getenv("PS1");
    }
    if (ps1_var && *ps1_var) {
        free(config->ps1_override);
        config->ps1_override = strdup(ps1_var);
    }
    
    // Check $LUSH_PS2 or $PS2
    const char *ps2_var = getenv("LUSH_PS2");
    if (!ps2_var) {
        ps2_var = getenv("PS2");
    }
    if (ps2_var && *ps2_var) {
        free(config->ps2_override);
        config->ps2_override = strdup(ps2_var);
    }
    
    // Check $RPROMPT
    const char *rprompt_var = getenv("LUSH_RPROMPT");
    if (!rprompt_var) {
        rprompt_var = getenv("RPROMPT");
    }
    if (rprompt_var && *rprompt_var) {
        free(config->rprompt_override);
        config->rprompt_override = strdup(rprompt_var);
    }
    
    // Check $LUSH_SYMBOL_MODE
    const char *symbol_var = getenv("LUSH_SYMBOL_MODE");
    if (symbol_var) {
        if (strcmp(symbol_var, "unicode") == 0) {
            config->symbol_mode = LLE_SYMBOL_MODE_UNICODE;
        } else if (strcmp(symbol_var, "ascii") == 0) {
            config->symbol_mode = LLE_SYMBOL_MODE_ASCII;
        }
    }
    
    // Check $COLORTERM for color mode
    const char *colorterm = getenv("COLORTERM");
    if (config->color_mode == LLE_COLOR_MODE_AUTO) {
        if (colorterm) {
            if (strstr(colorterm, "truecolor") || strstr(colorterm, "24bit")) {
                config->color_mode = LLE_COLOR_MODE_TRUECOLOR;
            }
        }
    }
    
    return LLE_PROMPT_SUCCESS;
}
```

### 10.6 Configuration Loading Order

```c
/**
 * Load complete configuration following hierarchy
 */
lle_prompt_result_t lle_prompt_config_load(lle_prompt_config_t *config) {
    if (!config) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    // Initialize with defaults
    lle_prompt_config_init_defaults(config);
    
    // Load system config
    if (lle_prompt_config_load_toml(config, "/etc/lush/prompt.toml") == LLE_PROMPT_SUCCESS) {
        config->system_config_path = strdup("/etc/lush/prompt.toml");
        config->theme_source = LLE_CONFIG_SOURCE_SYSTEM;
    }
    
    // Load user config (overrides system)
    char user_config[PATH_MAX];
    const char *home = getenv("HOME");
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    
    if (xdg_config) {
        snprintf(user_config, sizeof(user_config), "%s/lush/prompt.toml", xdg_config);
    } else if (home) {
        snprintf(user_config, sizeof(user_config), "%s/.config/lush/prompt.toml", home);
    }
    
    if (user_config[0] && 
        lle_prompt_config_load_toml(config, user_config) == LLE_PROMPT_SUCCESS) {
        config->user_config_path = strdup(user_config);
        config->theme_source = LLE_CONFIG_SOURCE_USER;
    }
    
    // Apply shell variable overrides (highest priority)
    lle_prompt_config_apply_shell_vars(config);
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Initialize configuration with built-in defaults
 */
static void lle_prompt_config_init_defaults(lle_prompt_config_t *config) {
    memset(config, 0, sizeof(*config));
    
    strncpy(config->theme_name, "default", sizeof(config->theme_name) - 1);
    config->theme_source = LLE_CONFIG_SOURCE_BUILTIN;
    
    config->async.enabled = true;
    config->async.git_timeout_ms = 1000;
    config->async.max_queue_size = 8;
    
    config->transient.enabled = false;
    config->transient.template = NULL;
    
    config->cache.max_entries = 64;
    config->cache.default_ttl = 300;
    
    config->symbol_mode = LLE_SYMBOL_MODE_AUTO;
    config->color_mode = LLE_COLOR_MODE_AUTO;
}
```

---

## 11. Built-in Themes and Segments

This section defines the default themes and segments shipped with lush.
All built-in components use the same registration API as user components,
ensuring first-class citizenship for all.

### 11.1 Built-in Segments

```c
/**
 * Built-in segment definitions
 * These are registered at startup using the same API as user segments
 */

// Forward declarations
static lle_prompt_result_t segment_username_render(const lle_segment_context_t *ctx, 
                                                    char *output, size_t output_size);
static lle_prompt_result_t segment_hostname_render(const lle_segment_context_t *ctx, 
                                                    char *output, size_t output_size);
static lle_prompt_result_t segment_directory_render(const lle_segment_context_t *ctx, 
                                                     char *output, size_t output_size);
static lle_prompt_result_t segment_git_render(const lle_segment_context_t *ctx, 
                                               char *output, size_t output_size);
static lle_prompt_result_t segment_status_render(const lle_segment_context_t *ctx, 
                                                  char *output, size_t output_size);
static lle_prompt_result_t segment_time_render(const lle_segment_context_t *ctx, 
                                                char *output, size_t output_size);
static lle_prompt_result_t segment_jobs_render(const lle_segment_context_t *ctx, 
                                                char *output, size_t output_size);

/**
 * Register all built-in segments
 */
lle_prompt_result_t lle_prompt_register_builtin_segments(lle_prompt_context_t *ctx) {
    lle_segment_definition_t segments[] = {
        {
            .name = "username",
            .render = segment_username_render,
            .properties = (const char*[]){"full", "short", NULL},
            .flags = LLE_SEGMENT_FLAG_CACHEABLE,
        },
        {
            .name = "hostname",
            .render = segment_hostname_render,
            .properties = (const char*[]){"full", "short", NULL},
            .flags = LLE_SEGMENT_FLAG_CACHEABLE,
        },
        {
            .name = "directory",
            .render = segment_directory_render,
            .properties = (const char*[]){"full", "short", "basename", NULL},
            .flags = 0,  // Not cached - changes with cd
        },
        {
            .name = "git",
            .render = segment_git_render,
            .properties = (const char*[]){"branch", "status", "ahead", "behind", 
                                          "staged", "unstaged", "untracked", NULL},
            .flags = LLE_SEGMENT_FLAG_ASYNC,
        },
        {
            .name = "status",
            .render = segment_status_render,
            .properties = (const char*[]){"code", "symbol", NULL},
            .flags = 0,
        },
        {
            .name = "time",
            .render = segment_time_render,
            .properties = (const char*[]){"full", "short", "iso", NULL},
            .flags = 0,
        },
        {
            .name = "jobs",
            .render = segment_jobs_render,
            .properties = (const char*[]){"count", "symbol", NULL},
            .flags = 0,
        },
    };
    
    for (size_t i = 0; i < sizeof(segments) / sizeof(segments[0]); i++) {
        lle_prompt_result_t result = lle_segment_register(ctx, &segments[i]);
        if (result != LLE_PROMPT_SUCCESS) {
            return result;
        }
    }
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Username segment implementation
 */
static lle_prompt_result_t segment_username_render(
    const lle_segment_context_t *ctx,
    char *output,
    size_t output_size
) {
    const char *user = getenv("USER");
    if (!user) {
        user = getenv("LOGNAME");
    }
    if (!user) {
        struct passwd *pw = getpwuid(getuid());
        user = pw ? pw->pw_name : "?";
    }
    
    if (ctx->property && strcmp(ctx->property, "short") == 0) {
        // First character only
        snprintf(output, output_size, "%c", user[0]);
    } else {
        snprintf(output, output_size, "%s", user);
    }
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Hostname segment implementation
 */
static lle_prompt_result_t segment_hostname_render(
    const lle_segment_context_t *ctx,
    char *output,
    size_t output_size
) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        snprintf(output, output_size, "?");
        return LLE_PROMPT_SUCCESS;
    }
    
    if (ctx->property && strcmp(ctx->property, "short") == 0) {
        // Up to first dot
        char *dot = strchr(hostname, '.');
        if (dot) *dot = '\0';
    }
    
    snprintf(output, output_size, "%s", hostname);
    return LLE_PROMPT_SUCCESS;
}

/**
 * Directory segment implementation
 */
static lle_prompt_result_t segment_directory_render(
    const lle_segment_context_t *ctx,
    char *output,
    size_t output_size
) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        snprintf(output, output_size, "?");
        return LLE_PROMPT_SUCCESS;
    }
    
    // Replace home with ~
    const char *home = getenv("HOME");
    char *display_path = cwd;
    char tilde_path[PATH_MAX];
    
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        snprintf(tilde_path, sizeof(tilde_path), "~%s", cwd + strlen(home));
        display_path = tilde_path;
    }
    
    if (ctx->property) {
        if (strcmp(ctx->property, "basename") == 0) {
            const char *basename = strrchr(display_path, '/');
            display_path = basename ? (char*)(basename + 1) : display_path;
            if (!*display_path) display_path = "/";
        } else if (strcmp(ctx->property, "short") == 0) {
            // Truncate to last N components based on config
            int max_depth = 3;  // Default, should come from config
            
            // Count path components
            int depth = 0;
            for (const char *p = display_path; *p; p++) {
                if (*p == '/') depth++;
            }
            
            if (depth > max_depth) {
                // Find the Nth-from-last slash
                const char *p = display_path + strlen(display_path);
                int count = 0;
                while (p > display_path && count < max_depth) {
                    p--;
                    if (*p == '/') count++;
                }
                if (count == max_depth) {
                    snprintf(output, output_size, "...%s", p);
                    return LLE_PROMPT_SUCCESS;
                }
            }
        }
    }
    
    snprintf(output, output_size, "%s", display_path);
    return LLE_PROMPT_SUCCESS;
}

/**
 * Git segment implementation
 */
static lle_prompt_result_t segment_git_render(
    const lle_segment_context_t *ctx,
    char *output,
    size_t output_size
) {
    // Try to get cached git status
    lle_git_status_data_t *status = lle_prompt_cache_get(
        ctx->prompt_ctx->cache, "git_status");
    
    if (!status || !status->is_git_repo) {
        output[0] = '\0';
        return LLE_PROMPT_SUCCESS;
    }
    
    if (ctx->property) {
        if (strcmp(ctx->property, "branch") == 0) {
            snprintf(output, output_size, "%s", status->branch);
        } else if (strcmp(ctx->property, "ahead") == 0) {
            if (status->ahead_count > 0) {
                snprintf(output, output_size, "%d", status->ahead_count);
            } else {
                output[0] = '\0';
            }
        } else if (strcmp(ctx->property, "behind") == 0) {
            if (status->behind_count > 0) {
                snprintf(output, output_size, "%d", status->behind_count);
            } else {
                output[0] = '\0';
            }
        } else if (strcmp(ctx->property, "staged") == 0) {
            if (status->staged_count > 0) {
                snprintf(output, output_size, "%d", status->staged_count);
            } else {
                output[0] = '\0';
            }
        } else if (strcmp(ctx->property, "unstaged") == 0) {
            if (status->unstaged_count > 0) {
                snprintf(output, output_size, "%d", status->unstaged_count);
            } else {
                output[0] = '\0';
            }
        } else if (strcmp(ctx->property, "untracked") == 0) {
            if (status->untracked_count > 0) {
                snprintf(output, output_size, "%d", status->untracked_count);
            } else {
                output[0] = '\0';
            }
        } else if (strcmp(ctx->property, "status") == 0) {
            // Combined status indicator
            char indicator[16] = "";
            if (status->staged_count > 0) strcat(indicator, "+");
            if (status->unstaged_count > 0) strcat(indicator, "!");
            if (status->untracked_count > 0) strcat(indicator, "?");
            snprintf(output, output_size, "%s", indicator);
        }
    } else {
        // Default: branch with status
        char status_str[16] = "";
        if (status->staged_count > 0) strcat(status_str, "+");
        if (status->unstaged_count > 0) strcat(status_str, "!");
        if (status->untracked_count > 0) strcat(status_str, "?");
        
        if (status_str[0]) {
            snprintf(output, output_size, "%s %s", status->branch, status_str);
        } else {
            snprintf(output, output_size, "%s", status->branch);
        }
    }
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Status segment (exit code) implementation
 */
static lle_prompt_result_t segment_status_render(
    const lle_segment_context_t *ctx,
    char *output,
    size_t output_size
) {
    int exit_code = ctx->prompt_ctx->last_exit_code;
    
    if (ctx->property && strcmp(ctx->property, "code") == 0) {
        if (exit_code != 0) {
            snprintf(output, output_size, "%d", exit_code);
        } else {
            output[0] = '\0';
        }
    } else {
        // Symbol mode - show indicator on failure
        if (exit_code != 0) {
            snprintf(output, output_size, "%s", 
                     ctx->prompt_ctx->symbols.error ?: "✗");
        } else {
            output[0] = '\0';
        }
    }
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * Time segment implementation
 */
static lle_prompt_result_t segment_time_render(
    const lle_segment_context_t *ctx,
    char *output,
    size_t output_size
) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    
    const char *format = "%H:%M";  // Default
    
    if (ctx->property) {
        if (strcmp(ctx->property, "full") == 0) {
            format = "%H:%M:%S";
        } else if (strcmp(ctx->property, "iso") == 0) {
            format = "%Y-%m-%d %H:%M:%S";
        }
    }
    
    strftime(output, output_size, format, tm);
    return LLE_PROMPT_SUCCESS;
}

/**
 * Jobs segment implementation
 */
static lle_prompt_result_t segment_jobs_render(
    const lle_segment_context_t *ctx,
    char *output,
    size_t output_size
) {
    int job_count = ctx->prompt_ctx->background_job_count;
    
    if (job_count == 0) {
        output[0] = '\0';
        return LLE_PROMPT_SUCCESS;
    }
    
    if (ctx->property && strcmp(ctx->property, "count") == 0) {
        snprintf(output, output_size, "%d", job_count);
    } else {
        snprintf(output, output_size, "%s%d", 
                 ctx->prompt_ctx->symbols.jobs ?: "⚙", job_count);
    }
    
    return LLE_PROMPT_SUCCESS;
}
```

### 11.2 Built-in Themes

```c
/**
 * Built-in theme definitions
 * Migrated from current themes.c
 */

static const lle_theme_definition_t builtin_themes[] = {
    // Default theme - simple and clean
    {
        .name = "default",
        .parent = NULL,
        .templates = {
            .ps1 = "${username}@${hostname}:${directory} ${symbol.prompt} ",
            .ps2 = "${symbol.continuation} ",
            .rprompt = NULL,
        },
        .colors = {
            .username = LLE_COLOR_GREEN,
            .hostname = LLE_COLOR_GREEN,
            .directory = LLE_COLOR_BLUE,
            .git_branch = LLE_COLOR_MAGENTA,
            .git_dirty = LLE_COLOR_RED,
            .git_clean = LLE_COLOR_GREEN,
            .error = LLE_COLOR_RED,
        },
        .symbols = {
            .prompt = "$",
            .prompt_root = "#",
            .continuation = ">",
            .git_branch = "",
            .git_staged = "+",
            .git_unstaged = "!",
            .git_untracked = "?",
        },
    },
    
    // Minimal theme - for purists
    {
        .name = "minimal",
        .parent = NULL,
        .templates = {
            .ps1 = "${directory.basename} ${symbol.prompt} ",
            .ps2 = "${symbol.continuation} ",
            .rprompt = NULL,
        },
        .colors = {
            .directory = LLE_COLOR_BLUE,
        },
        .symbols = {
            .prompt = "❯",
            .prompt_root = "#",
            .continuation = "…",
        },
    },
    
    // Powerline theme - feature-rich
    {
        .name = "powerline",
        .parent = NULL,
        .templates = {
            .ps1 = "${?status:${color.error}${status.code} }${color.blue}${directory.short}"
                   "${?git: ${color.magenta}${symbol.git_branch} ${git.branch}${git.status}}"
                   " ${symbol.prompt} ",
            .ps2 = "${symbol.continuation} ",
            .rprompt = "${time.short}",
        },
        .colors = {
            .username = LLE_COLOR_CYAN,
            .hostname = LLE_COLOR_CYAN,
            .directory = LLE_COLOR_BLUE,
            .git_branch = LLE_COLOR_MAGENTA,
            .git_dirty = LLE_COLOR_YELLOW,
            .git_clean = LLE_COLOR_GREEN,
            .error = LLE_COLOR_RED,
            .time = LLE_COLOR_WHITE,
        },
        .symbols = {
            .prompt = "❯",
            .prompt_root = "#",
            .continuation = "…",
            .git_branch = "",
            .git_staged = "+",
            .git_unstaged = "!",
            .git_untracked = "?",
            .separator = "",
            .separator_thin = "",
        },
        .transient = {
            .enabled = true,
            .template = "${symbol.prompt} ",
        },
    },
    
    // Classic theme - bash-like
    {
        .name = "classic",
        .parent = NULL,
        .templates = {
            .ps1 = "[${username}@${hostname.short} ${directory.basename}]${symbol.prompt} ",
            .ps2 = "> ",
            .rprompt = NULL,
        },
        .symbols = {
            .prompt = "$",
            .prompt_root = "#",
        },
    },
    
    // Informative theme - detailed info
    {
        .name = "informative",
        .parent = NULL,
        .templates = {
            .ps1 = "${color.cyan}${username}${color.reset}@${color.cyan}${hostname}${color.reset}:"
                   "${color.blue}${directory}${color.reset}"
                   "${?git:\n${color.magenta}git:(${git.branch})${color.reset}"
                   "${?git.ahead: ↑${git.ahead}}${?git.behind: ↓${git.behind}}"
                   "${?git.staged: ${color.green}+${git.staged}${color.reset}}"
                   "${?git.unstaged: ${color.yellow}!${git.unstaged}${color.reset}}"
                   "${?git.untracked: ${color.red}?${git.untracked}${color.reset}}}"
                   "\n${?status:${color.red}[${status.code}]${color.reset} }${symbol.prompt} ",
            .ps2 = "${symbol.continuation} ",
            .rprompt = "${time} ${?jobs:[${jobs.count}]}",
        },
        .colors = {
            .username = LLE_COLOR_CYAN,
            .hostname = LLE_COLOR_CYAN,
            .directory = LLE_COLOR_BLUE,
            .git_branch = LLE_COLOR_MAGENTA,
            .error = LLE_COLOR_RED,
        },
        .symbols = {
            .prompt = "❯",
            .prompt_root = "#",
            .continuation = "...",
        },
    },
    
    // Two-line theme
    {
        .name = "two-line",
        .parent = NULL,
        .templates = {
            .ps1 = "┌─[${username}@${hostname}]─[${directory}]${?git:─[${git.branch}${git.status}]}\n"
                   "└─${symbol.prompt} ",
            .ps2 = "   ${symbol.continuation} ",
            .rprompt = NULL,
        },
        .colors = {
            .username = LLE_COLOR_GREEN,
            .hostname = LLE_COLOR_GREEN,
            .directory = LLE_COLOR_BLUE,
            .git_branch = LLE_COLOR_YELLOW,
        },
        .symbols = {
            .prompt = "❯",
            .prompt_root = "#",
            .continuation = "…",
        },
    },
};

/**
 * Register all built-in themes
 */
lle_prompt_result_t lle_prompt_register_builtin_themes(lle_prompt_context_t *ctx) {
    for (size_t i = 0; i < sizeof(builtin_themes) / sizeof(builtin_themes[0]); i++) {
        lle_prompt_result_t result = lle_theme_register(ctx, &builtin_themes[i]);
        if (result != LLE_PROMPT_SUCCESS) {
            return result;
        }
    }
    
    return LLE_PROMPT_SUCCESS;
}
```

### 11.3 Symbol Sets

```c
/**
 * Symbol sets for different terminal capabilities
 */
typedef struct {
    const char *name;
    lle_theme_symbols_t symbols;
} lle_symbol_set_t;

static const lle_symbol_set_t symbol_sets[] = {
    {
        .name = "unicode",
        .symbols = {
            .prompt = "❯",
            .prompt_root = "#",
            .continuation = "…",
            .git_branch = "",
            .git_staged = "✚",
            .git_unstaged = "●",
            .git_untracked = "…",
            .git_ahead = "⇡",
            .git_behind = "⇣",
            .separator = "",
            .separator_thin = "",
            .error = "✗",
            .success = "✓",
            .jobs = "⚙",
            .root = "⚡",
        },
    },
    {
        .name = "nerd",  // Nerd Fonts
        .symbols = {
            .prompt = "",
            .prompt_root = "",
            .continuation = "",
            .git_branch = "",
            .git_staged = "",
            .git_unstaged = "",
            .git_untracked = "",
            .git_ahead = "",
            .git_behind = "",
            .separator = "",
            .separator_thin = "",
            .error = "",
            .success = "",
            .jobs = "",
            .root = "",
        },
    },
    {
        .name = "ascii",
        .symbols = {
            .prompt = "$",
            .prompt_root = "#",
            .continuation = ">",
            .git_branch = "",
            .git_staged = "+",
            .git_unstaged = "!",
            .git_untracked = "?",
            .git_ahead = "^",
            .git_behind = "v",
            .separator = ">",
            .separator_thin = "|",
            .error = "X",
            .success = "OK",
            .jobs = "*",
            .root = "!",
        },
    },
};

/**
 * Get symbol set by name
 */
const lle_symbol_set_t *lle_prompt_get_symbol_set(const char *name) {
    for (size_t i = 0; i < sizeof(symbol_sets) / sizeof(symbol_sets[0]); i++) {
        if (strcmp(symbol_sets[i].name, name) == 0) {
            return &symbol_sets[i];
        }
    }
    return NULL;
}

/**
 * Auto-detect best symbol set for terminal
 */
const lle_symbol_set_t *lle_prompt_detect_symbol_set(void) {
    // Check for known terminals with good Unicode support
    const char *term = getenv("TERM");
    const char *term_program = getenv("TERM_PROGRAM");
    
    // Check for Nerd Font indicator
    const char *nerd_font = getenv("NERD_FONT");
    if (nerd_font && strcmp(nerd_font, "1") == 0) {
        return lle_prompt_get_symbol_set("nerd");
    }
    
    // Modern terminals generally support Unicode
    if (term_program) {
        if (strstr(term_program, "iTerm") ||
            strstr(term_program, "Alacritty") ||
            strstr(term_program, "kitty") ||
            strstr(term_program, "WezTerm")) {
            return lle_prompt_get_symbol_set("unicode");
        }
    }
    
    // Check LANG for UTF-8
    const char *lang = getenv("LANG");
    if (lang && (strstr(lang, "UTF-8") || strstr(lang, "utf-8"))) {
        return lle_prompt_get_symbol_set("unicode");
    }
    
    // Fall back to ASCII
    return lle_prompt_get_symbol_set("ascii");
}
```

---

## 12. Transient Prompt System

The transient prompt feature simplifies previous prompts after command execution,
reducing visual clutter and highlighting the current prompt. This is inspired by
starship's transient prompt feature.

### 12.0 Implementation Notes (Session 73 - 2025-12-27)

> **IMPORTANT**: The actual implementation differs from the specification below.
> The specification describes a complex approach with direct terminal manipulation.
> The implemented solution uses a simpler, more robust architecture that integrates
> with LLE's existing systems.

**Actual Implementation Architecture**:

1. **Widget Hook Approach**: Instead of the pre-command event handler approach 
   described in Section 8.3, transient prompts use a dedicated widget hook:
   - New hook type: `LLE_HOOK_LINE_ACCEPTED`
   - Fires after Enter key but before `dc_finalize_input()`
   - Registered widget: `widget_transient_prompt()`

2. **Display Controller Integration**: Instead of direct terminal escape sequences,
   the implementation uses `dc_apply_transient_prompt()` which:
   - Uses `current_screen.cursor_row` to know display height
   - Moves cursor up to row 0 using `\033[nA`
   - Clears from cursor to end of screen using `\033[J`
   - Writes transient prompt + command text
   - Updates screen buffer state via `screen_buffer_render()`

3. **Config System Integration**:
   - Global config: `config.display_transient_prompt` (default: true)
   - Composer config: `composer->config.enable_transient`
   - Theme config: `theme->layout.enable_transient` and `theme->layout.transient_format`
   - User command: `display lle transient on|off`

4. **Simplified State**: Instead of the complex `lle_transient_state_t` struct in
   the spec, the implementation relies on the screen buffer's existing tracking
   of cursor position and uses that for transient replacement.

**Key Files**:
- `include/lle/widget_hooks.h` - LLE_HOOK_LINE_ACCEPTED definition
- `src/lle/widget/builtin_widgets.c` - widget_transient_prompt()
- `src/display/display_controller.c` - dc_apply_transient_prompt()
- `include/lle/prompt/transient.h` - Simplified state struct (constants only)

**Why This Approach**:
- Respects LLE's internal state authority model (no terminal queries)
- Uses screen buffer for accurate position tracking
- Integrates with existing widget hook system
- Simpler, more maintainable code
- Works with all themes via template engine

### 12.1 Transient Prompt Mechanics

```
Before command execution:
┌─[user@host]─[~/projects/lush]─[feature/lle +3!2]
└─❯ make

After command execution (transient applied to previous prompt):
❯ make
...build output...
┌─[user@host]─[~/projects/lush]─[feature/lle +3!2]
└─❯ 
```

### 12.2 Transient Prompt Configuration

```c
/**
 * Transient prompt configuration
 */
typedef struct {
    bool enabled;                    // Enable transient prompt
    char *template;                  // Template for transient prompt
    bool preserve_on_error;          // Keep full prompt if command failed
    bool apply_to_rprompt;           // Also simplify right prompt
    uint32_t max_history_lines;      // How many previous prompts to transient
} lle_transient_config_t;

/**
 * Transient prompt state
 */
typedef struct {
    // Previous prompt location
    int original_start_line;
    int original_end_line;
    int original_start_col;
    
    // Original prompt content (for restore if needed)
    char *original_prompt;
    char *original_rprompt;
    
    // Whether transient has been applied
    bool applied;
} lle_transient_state_t;
```

### 12.3 Transient Prompt Implementation

```c
/**
 * @brief Apply transient prompt to previous prompt
 *
 * Called from LLE_SHELL_EVENT_PRE_COMMAND handler (Section 8.3) before
 * command execution begins. This replaces the current full prompt with
 * a simplified version to reduce visual clutter.
 *
 * @param composer The prompt composer
 * @return LLE_PROMPT_SUCCESS or error code
 */
lle_prompt_result_t lle_prompt_apply_transient(lle_prompt_composer_t *composer) {
    if (!composer || !composer->current_theme) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    /* Check if transient is enabled */
    if (!composer->current_theme->layout.enable_transient) {
        return LLE_PROMPT_SUCCESS;
    }
    
    /* Check if we should preserve on error */
    if (composer->transient_config.preserve_on_error && 
        composer->context.last_exit_code != 0) {
        return LLE_PROMPT_SUCCESS;
    }
    
    /* Check if we have a previous prompt to replace */
    if (composer->transient_state.original_start_line < 0) {
        return LLE_PROMPT_SUCCESS;
    }
    
    /* Get transient template from theme layout */
    const char *template = composer->current_theme->layout.transient_format;
    if (!template || strlen(template) == 0) {
        template = "${symbol.prompt} ";  /* Default fallback */
    }
    
    /* Render transient prompt */
    char transient_prompt[LLE_PROMPT_OUTPUT_MAX];
    lle_prompt_result_t result = lle_template_render(
        composer->transient_template,
        &composer->context,
        composer->current_theme,
        composer->segment_registry,
        transient_prompt,
        sizeof(transient_prompt)
    );
    
    if (result != LLE_PROMPT_SUCCESS) {
        return result;
    }
    
    /* Apply transient to screen */
    result = lle_prompt_transient_overwrite(composer, transient_prompt);
    
    composer->transient_state.applied = true;
    
    return result;
}

/**
 * @brief Overwrite previous prompt with transient version
 *
 * @param composer The prompt composer
 * @param transient_prompt The rendered transient prompt string
 * @return LLE_PROMPT_SUCCESS or error code
 */
static lle_prompt_result_t lle_prompt_transient_overwrite(
    lle_prompt_composer_t *composer,
    const char *transient_prompt
) {
    lle_prompt_display_t *display = &composer->display;
    
    /* Save current cursor position */
    int saved_x = display->screen->cursor_x;
    int saved_y = display->screen->cursor_y;
    
    /* Move to start of original prompt */
    lle_screen_buffer_move_cursor(
        display->screen,
        composer->transient_state.original_start_col,
        composer->transient_state.original_start_line
    );
    
    /* Clear the original prompt lines */
    for (int y = composer->transient_state.original_start_line;
         y <= composer->transient_state.original_end_line;
         y++) {
        lle_screen_buffer_clear_line(display->screen, y);
    }
    
    /* If original prompt was multi-line, we need to collapse */
    int lines_removed = composer->transient_state.original_end_line - 
                        composer->transient_state.original_start_line;
    
    if (lines_removed > 0) {
        /* Scroll content up to remove extra lines */
        lle_screen_buffer_scroll_region(
            display->screen,
            composer->transient_state.original_start_line + 1,
            display->screen->height - 1,
            -lines_removed
        );
    }
    
    /* Move back to first line */
    lle_screen_buffer_move_cursor(
        display->screen,
        composer->transient_state.original_start_col,
        composer->transient_state.original_start_line
    );
    
    /* Write transient prompt */
    lle_prompt_display_write_styled(display, transient_prompt);
    
    /* Clear right prompt if configured */
    if (composer->transient_config.apply_to_rprompt) {
        int rprompt_start = display->rprompt_col;
        for (int x = rprompt_start; x < display->screen->width; x++) {
            lle_screen_buffer_set_cell(display->screen, x, 
                                        composer->transient_state.original_start_line,
                                        ' ', (lle_cell_style_t){0});
        }
    }
    
    /* Restore cursor position (adjusted for removed lines) */
    lle_screen_buffer_move_cursor(
        display->screen,
        saved_x,
        saved_y - lines_removed
    );
    
    /* Flush changes */
    lle_screen_buffer_flush(display->screen);
    
    return LLE_PROMPT_SUCCESS;
}

/**
 * @brief Record current prompt position for later transient application
 *
 * Called during prompt rendering to save position for transient replacement.
 *
 * @param composer The prompt composer
 */
void lle_prompt_transient_record(lle_prompt_composer_t *composer) {
    if (!composer) return;
    
    /* Store original prompt location */
    composer->transient_state.original_start_line = composer->display.ps1_start_line;
    composer->transient_state.original_end_line = composer->display.ps1_end_line;
    composer->transient_state.original_start_col = 0;
    
    /* Store original prompt content (for potential restore) */
    free(composer->transient_state.original_prompt);
    composer->transient_state.original_prompt = 
        composer->cached_prompt ? strdup(composer->cached_prompt) : NULL;
    
    free(composer->transient_state.original_rprompt);
    composer->transient_state.original_rprompt = 
        composer->cached_rprompt ? strdup(composer->cached_rprompt) : NULL;
    
    composer->transient_state.applied = false;
}

/**
 * @brief Reset transient state (after new prompt is drawn)
 *
 * @param composer The prompt composer
 */
void lle_prompt_transient_reset(lle_prompt_composer_t *composer) {
    if (!composer) return;
    
    composer->transient_state.original_start_line = -1;
    composer->transient_state.original_end_line = -1;
    
    free(composer->transient_state.original_prompt);
    composer->transient_state.original_prompt = NULL;
    
    free(composer->transient_state.original_rprompt);
    composer->transient_state.original_rprompt = NULL;
    
    composer->transient_state.applied = false;
}
```

### 12.4 Transient with Terminal Scrollback

```c
/**
 * Handle transient prompt with terminal scrollback buffer
 * 
 * When using transient prompts with a terminal's scrollback buffer,
 * we need to use terminal escape sequences rather than screen buffer
 * manipulation for prompts that have scrolled off.
 */
lle_prompt_result_t lle_prompt_transient_terminal(
    lle_prompt_context_t *ctx,
    const char *transient_prompt
) {
    // Check if original prompt is still visible
    int screen_top = 0;  // Would need terminal query
    
    if (ctx->transient_state.original_start_line < screen_top) {
        // Prompt has scrolled off - can't apply transient
        // This is expected behavior for long command outputs
        return LLE_PROMPT_SUCCESS;
    }
    
    // Use ANSI cursor positioning for visible prompts
    // Save cursor: \033[s
    // Move to line: \033[<line>H
    // Clear line: \033[2K
    // Write transient
    // Restore cursor: \033[u
    
    char buf[256];
    int len = 0;
    
    // Save cursor
    len += snprintf(buf + len, sizeof(buf) - len, "\033[s");
    
    // Move to original prompt line
    len += snprintf(buf + len, sizeof(buf) - len, "\033[%d;%dH",
                    ctx->transient_state.original_start_line + 1,
                    ctx->transient_state.original_start_col + 1);
    
    // Clear from cursor to end of line
    len += snprintf(buf + len, sizeof(buf) - len, "\033[K");
    
    // Write to terminal
    write(STDOUT_FILENO, buf, len);
    
    // Write transient prompt
    write(STDOUT_FILENO, transient_prompt, strlen(transient_prompt));
    
    // If multi-line original, clear remaining lines
    for (int y = ctx->transient_state.original_start_line + 1;
         y <= ctx->transient_state.original_end_line;
         y++) {
        char clear_buf[32];
        int clear_len = snprintf(clear_buf, sizeof(clear_buf), 
                                  "\033[%d;1H\033[2K", y + 1);
        write(STDOUT_FILENO, clear_buf, clear_len);
    }
    
    // Restore cursor
    write(STDOUT_FILENO, "\033[u", 3);
    
    return LLE_PROMPT_SUCCESS;
}
```

---

## 13. External Prompt Integration

Support for external prompt generators like starship, oh-my-posh, or custom scripts.

### 13.1 External Prompt Interface

```c
/**
 * External prompt configuration
 */
typedef struct {
    char *command;           // Command to execute for prompt
    char **argv;             // Arguments
    uint32_t timeout_ms;     // Timeout for external command
    bool use_stderr;         // Capture stderr as well
    bool async;              // Run asynchronously
    char *fallback;          // Fallback prompt if command fails
} lle_external_prompt_config_t;

/**
 * External prompt result
 */
typedef struct {
    char *ps1;               // Primary prompt
    char *ps2;               // Continuation prompt
    char *rprompt;           // Right prompt
    int exit_code;           // Command exit code
    uint32_t duration_ms;    // Time taken
} lle_external_prompt_result_t;
```

### 13.2 External Prompt Execution

```c
/**
 * Execute external prompt generator
 */
lle_prompt_result_t lle_prompt_external_execute(
    const lle_external_prompt_config_t *config,
    lle_external_prompt_result_t *result
) {
    if (!config || !config->command || !result) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    memset(result, 0, sizeof(*result));
    
    // Set up environment for external prompt
    // These are commonly expected by prompt generators
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));
    setenv("PWD", cwd, 1);
    
    // Last command status
    char status_buf[16];
    snprintf(status_buf, sizeof(status_buf), "%d", 
             lle_prompt_get_last_exit_code());
    setenv("PROMPT_LAST_EXIT_CODE", status_buf, 1);
    setenv("?", status_buf, 1);  // For compatibility
    
    // Command duration (if available)
    char duration_buf[32];
    snprintf(duration_buf, sizeof(duration_buf), "%u",
             lle_prompt_get_last_cmd_duration());
    setenv("PROMPT_CMD_DURATION", duration_buf, 1);
    
    // Number of jobs
    char jobs_buf[16];
    snprintf(jobs_buf, sizeof(jobs_buf), "%d",
             lle_prompt_get_job_count());
    setenv("PROMPT_JOBS", jobs_buf, 1);
    
    // Terminal width
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        char cols_buf[16];
        snprintf(cols_buf, sizeof(cols_buf), "%d", ws.ws_col);
        setenv("COLUMNS", cols_buf, 1);
    }
    
    // Execute command
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return LLE_PROMPT_ERROR_MEMORY_ALLOCATION;
    }
    
    if (pid == 0) {
        // Child
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        if (config->use_stderr) {
            dup2(pipefd[1], STDERR_FILENO);
        }
        close(pipefd[1]);
        
        if (config->argv) {
            execvp(config->command, config->argv);
        } else {
            execlp(config->command, config->command, NULL);
        }
        _exit(127);
    }
    
    // Parent
    close(pipefd[1]);
    
    // Read output with timeout
    char output[4096];
    size_t output_len = 0;
    
    struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN};
    int remaining_ms = config->timeout_ms ?: 1000;
    
    while (remaining_ms > 0) {
        int ret = poll(&pfd, 1, remaining_ms);
        if (ret <= 0) break;
        
        ssize_t n = read(pipefd[0], output + output_len, 
                         sizeof(output) - output_len - 1);
        if (n <= 0) break;
        output_len += n;
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        uint32_t elapsed = (end.tv_sec - start.tv_sec) * 1000 +
                          (end.tv_nsec - start.tv_nsec) / 1000000;
        remaining_ms = config->timeout_ms - elapsed;
    }
    
    close(pipefd[0]);
    output[output_len] = '\0';
    
    // Wait for child
    int status;
    waitpid(pid, &status, 0);
    result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    result->duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                         (end.tv_nsec - start.tv_nsec) / 1000000;
    
    // Parse output
    // Convention: first line is PS1, optional second is RPROMPT
    char *newline = strchr(output, '\n');
    if (newline) {
        *newline = '\0';
        result->ps1 = strdup(output);
        
        char *rprompt = newline + 1;
        char *end = strchr(rprompt, '\n');
        if (end) *end = '\0';
        if (*rprompt) {
            result->rprompt = strdup(rprompt);
        }
    } else {
        result->ps1 = strdup(output);
    }
    
    return LLE_PROMPT_SUCCESS;
}
```

### 13.3 Starship Integration

```c
/**
 * Create starship prompt configuration
 */
lle_external_prompt_config_t *lle_prompt_starship_config(void) {
    lle_external_prompt_config_t *config = calloc(1, sizeof(*config));
    if (!config) return NULL;
    
    config->command = "starship";
    config->argv = (char*[]){
        "starship", "prompt",
        "--status", getenv("PROMPT_LAST_EXIT_CODE") ?: "0",
        "--cmd-duration", getenv("PROMPT_CMD_DURATION") ?: "0",
        "--jobs", getenv("PROMPT_JOBS") ?: "0",
        NULL
    };
    config->timeout_ms = 500;  // Starship should be fast
    config->fallback = "$ ";
    
    return config;
}

/**
 * Create starship right prompt configuration
 */
lle_external_prompt_config_t *lle_prompt_starship_rprompt_config(void) {
    lle_external_prompt_config_t *config = calloc(1, sizeof(*config));
    if (!config) return NULL;
    
    config->command = "starship";
    config->argv = (char*[]){
        "starship", "prompt", "--right",
        "--status", getenv("PROMPT_LAST_EXIT_CODE") ?: "0",
        "--cmd-duration", getenv("PROMPT_CMD_DURATION") ?: "0",
        NULL
    };
    config->timeout_ms = 500;
    config->fallback = "";
    
    return config;
}

/**
 * Theme that uses starship
 */
static const lle_theme_definition_t starship_theme = {
    .name = "starship",
    .parent = NULL,
    .flags = LLE_THEME_FLAG_EXTERNAL,
    .external = {
        .ps1_command = "starship prompt",
        .rprompt_command = "starship prompt --right",
        .timeout_ms = 500,
    },
};
```

---

## 14. Performance Requirements

The prompt system must be fast. Users notice prompt latency above 10ms.

### 14.1 Performance Targets

| Operation | Target | Maximum |
|-----------|--------|---------|
| Prompt generation (cached) | < 1ms | 5ms |
| Prompt generation (fresh) | < 5ms | 20ms |
| Template parsing | < 0.5ms | 2ms |
| Segment rendering (non-async) | < 0.1ms | 1ms |
| Git status (async) | N/A (background) | 1000ms timeout |
| Theme switching | < 10ms | 50ms |
| Config file parsing | < 5ms | 20ms |

### 14.2 Performance Measurement

```c
/**
 * Performance timing structure
 */
typedef struct {
    uint64_t template_parse_ns;
    uint64_t segment_render_ns[LLE_MAX_SEGMENTS];
    uint64_t cache_lookup_ns;
    uint64_t total_render_ns;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t segments_rendered;
} lle_prompt_perf_stats_t;

/**
 * Get current time in nanoseconds
 */
static inline uint64_t lle_perf_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/**
 * Performance measurement macros
 */
#ifdef LLE_PERF_DEBUG
#define LLE_PERF_START(name) uint64_t _perf_##name = lle_perf_now_ns()
#define LLE_PERF_END(name, stats) \
    (stats)->name##_ns = lle_perf_now_ns() - _perf_##name
#else
#define LLE_PERF_START(name) (void)0
#define LLE_PERF_END(name, stats) (void)0
#endif

/**
 * Get performance statistics
 */
void lle_prompt_get_perf_stats(
    lle_prompt_context_t *ctx,
    lle_prompt_perf_stats_t *stats
) {
    if (!ctx || !stats) return;
    memcpy(stats, &ctx->perf_stats, sizeof(*stats));
}

/**
 * Log performance warning if thresholds exceeded
 */
void lle_prompt_check_perf(lle_prompt_context_t *ctx) {
    if (!ctx) return;
    
    uint64_t total_ms = ctx->perf_stats.total_render_ns / 1000000;
    
    if (total_ms > 20) {
        // Log warning - prompt is too slow
        lle_log(LLE_LOG_WARN, "prompt", 
                "Prompt render took %lums (target: <5ms)", total_ms);
        
        // Identify slow segments
        for (size_t i = 0; i < ctx->perf_stats.segments_rendered; i++) {
            uint64_t seg_ms = ctx->perf_stats.segment_render_ns[i] / 1000000;
            if (seg_ms > 5) {
                lle_log(LLE_LOG_WARN, "prompt",
                        "Segment %zu took %lums", i, seg_ms);
            }
        }
    }
}
```

### 14.3 Optimization Strategies

```c
/**
 * Lazy evaluation - only render segments that are used
 */
typedef struct {
    bool evaluated;
    char output[LLE_SEGMENT_OUTPUT_MAX];
} lle_lazy_segment_t;

/**
 * Render segment only when accessed
 */
const char *lle_segment_get_lazy(
    lle_prompt_context_t *ctx,
    const char *segment_name,
    const char *property
) {
    // Check cache first
    char cache_key[128];
    snprintf(cache_key, sizeof(cache_key), "seg:%s.%s", 
             segment_name, property ?: "");
    
    lle_prompt_cache_entry_t *cached = lle_prompt_cache_get(&ctx->cache, cache_key);
    if (cached) {
        ctx->perf_stats.cache_hits++;
        return cached->data;
    }
    
    ctx->perf_stats.cache_misses++;
    
    // Render segment
    lle_segment_definition_t *seg = lle_segment_lookup(ctx, segment_name);
    if (!seg) return "";
    
    lle_segment_context_t seg_ctx = {
        .prompt_ctx = ctx,
        .property = property,
    };
    
    char *output = calloc(1, LLE_SEGMENT_OUTPUT_MAX);
    if (!output) return "";
    
    LLE_PERF_START(segment);
    seg->render(&seg_ctx, output, LLE_SEGMENT_OUTPUT_MAX);
    LLE_PERF_END(segment, &ctx->perf_stats);
    
    // Cache result
    if (seg->flags & LLE_SEGMENT_FLAG_CACHEABLE) {
        lle_prompt_cache_entry_t *entry = calloc(1, sizeof(*entry));
        if (entry) {
            entry->key = strdup(cache_key);
            entry->data = output;
            entry->data_size = strlen(output) + 1;
            entry->timestamp = time(NULL);
            entry->ttl = 60;  // 1 minute default
            lle_prompt_cache_set(&ctx->cache, entry);
        }
    }
    
    return output;
}

/**
 * String pooling for common substrings
 */
typedef struct {
    char **strings;
    size_t count;
    size_t capacity;
} lle_string_pool_t;

const char *lle_string_pool_intern(lle_string_pool_t *pool, const char *str) {
    // Check if string already exists
    for (size_t i = 0; i < pool->count; i++) {
        if (strcmp(pool->strings[i], str) == 0) {
            return pool->strings[i];
        }
    }
    
    // Add new string
    if (pool->count >= pool->capacity) {
        pool->capacity = pool->capacity ? pool->capacity * 2 : 64;
        pool->strings = realloc(pool->strings, pool->capacity * sizeof(char*));
    }
    
    pool->strings[pool->count] = strdup(str);
    return pool->strings[pool->count++];
}
```

---

## 15. Error Handling

Robust error handling ensures the prompt system never blocks or crashes.

### 15.1 Error Recovery Strategy

```c
/**
 * Error recovery levels
 */
typedef enum {
    LLE_RECOVERY_RETRY,      // Retry the operation
    LLE_RECOVERY_FALLBACK,   // Use fallback value
    LLE_RECOVERY_SKIP,       // Skip this component
    LLE_RECOVERY_ABORT,      // Abort and use minimal prompt
} lle_recovery_level_t;

/**
 * Error context for recovery decisions
 */
typedef struct {
    lle_prompt_result_t error;
    const char *component;
    int retry_count;
    void *context;
} lle_error_context_t;

/**
 * Determine recovery action for an error
 */
lle_recovery_level_t lle_prompt_get_recovery(
    const lle_error_context_t *err
) {
    switch (err->error) {
        case LLE_PROMPT_ERROR_MEMORY_ALLOCATION:
            // Memory errors are serious - abort
            return LLE_RECOVERY_ABORT;
            
        case LLE_PROMPT_ERROR_TIMEOUT:
            // Timeout - use cached or skip
            if (err->retry_count < 1) {
                return LLE_RECOVERY_RETRY;
            }
            return LLE_RECOVERY_FALLBACK;
            
        case LLE_PROMPT_ERROR_NOT_FOUND:
            // Missing component - skip it
            return LLE_RECOVERY_SKIP;
            
        case LLE_PROMPT_ERROR_INVALID_TEMPLATE:
            // Bad template - use raw text
            return LLE_RECOVERY_FALLBACK;
            
        default:
            return LLE_RECOVERY_FALLBACK;
    }
}

/**
 * Apply recovery action
 */
lle_prompt_result_t lle_prompt_apply_recovery(
    lle_prompt_context_t *ctx,
    const lle_error_context_t *err,
    lle_recovery_level_t level
) {
    switch (level) {
        case LLE_RECOVERY_RETRY:
            // Retry logic handled by caller
            return LLE_PROMPT_ERROR_RETRY;
            
        case LLE_RECOVERY_FALLBACK:
            // Use fallback prompt
            free(ctx->cached_prompt);
            ctx->cached_prompt = strdup(
                ctx->config.ps1_override ?: "$ ");
            return LLE_PROMPT_SUCCESS;
            
        case LLE_RECOVERY_SKIP:
            // Return empty for this component
            return LLE_PROMPT_SUCCESS;
            
        case LLE_RECOVERY_ABORT:
            // Use absolute minimal prompt
            free(ctx->cached_prompt);
            ctx->cached_prompt = strdup("$ ");
            lle_log(LLE_LOG_ERROR, "prompt",
                    "Critical error in %s, using minimal prompt",
                    err->component);
            return LLE_PROMPT_SUCCESS;
    }
    
    return LLE_PROMPT_ERROR_INVALID_PARAM;
}
```

### 15.2 Safe Prompt Rendering

```c
/**
 * Render prompt with full error handling
 */
lle_prompt_result_t lle_prompt_render_safe(
    lle_prompt_context_t *ctx,
    char **output
) {
    if (!ctx || !output) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    *output = NULL;
    
    // Try normal rendering
    lle_prompt_result_t result = lle_prompt_render(ctx, output);
    
    if (result == LLE_PROMPT_SUCCESS && *output) {
        return LLE_PROMPT_SUCCESS;
    }
    
    // Error occurred - try recovery
    lle_error_context_t err = {
        .error = result,
        .component = "render",
        .retry_count = 0,
        .context = ctx,
    };
    
    lle_recovery_level_t recovery = lle_prompt_get_recovery(&err);
    
    if (recovery == LLE_RECOVERY_RETRY) {
        err.retry_count++;
        result = lle_prompt_render(ctx, output);
        if (result == LLE_PROMPT_SUCCESS && *output) {
            return LLE_PROMPT_SUCCESS;
        }
        recovery = LLE_RECOVERY_FALLBACK;
    }
    
    // Apply fallback
    lle_prompt_apply_recovery(ctx, &err, recovery);
    *output = strdup(ctx->cached_prompt ?: "$ ");
    
    return LLE_PROMPT_SUCCESS;  // We recovered
}

/**
 * Segment rendering with timeout protection
 */
lle_prompt_result_t lle_segment_render_protected(
    const lle_segment_definition_t *seg,
    const lle_segment_context_t *ctx,
    char *output,
    size_t output_size,
    uint32_t timeout_ms
) {
    if (!seg || !ctx || !output) {
        return LLE_PROMPT_ERROR_INVALID_PARAM;
    }
    
    // For non-async segments, just render directly
    // (we trust built-in segments to be fast)
    if (!(seg->flags & LLE_SEGMENT_FLAG_ASYNC)) {
        return seg->render(ctx, output, output_size);
    }
    
    // For async segments, use cached value or empty
    const char *cached = lle_segment_get_cached(ctx->prompt_ctx, seg->name);
    if (cached) {
        strncpy(output, cached, output_size - 1);
        output[output_size - 1] = '\0';
        return LLE_PROMPT_SUCCESS;
    }
    
    // No cache - trigger async fetch and return empty for now
    lle_segment_trigger_async(ctx->prompt_ctx, seg);
    output[0] = '\0';
    
    return LLE_PROMPT_SUCCESS;
}
```

---

## 16. Testing and Validation

### 16.1 Unit Test Framework

```c
/**
 * Test case structure
 */
typedef struct {
    const char *name;
    bool (*test_fn)(void);
    const char *description;
} lle_prompt_test_case_t;

/**
 * Test result
 */
typedef struct {
    const char *test_name;
    bool passed;
    const char *error_message;
    uint64_t duration_ns;
} lle_prompt_test_result_t;

/**
 * Run a single test
 */
lle_prompt_test_result_t lle_prompt_run_test(
    const lle_prompt_test_case_t *test
) {
    lle_prompt_test_result_t result = {
        .test_name = test->name,
        .passed = false,
    };
    
    uint64_t start = lle_perf_now_ns();
    
    // Run test with exception handling
    result.passed = test->test_fn();
    
    result.duration_ns = lle_perf_now_ns() - start;
    
    return result;
}
```

### 16.2 Example Test Cases

```c
/**
 * Test template parsing
 */
static bool test_template_parse_simple(void) {
    const char *template = "${username}@${hostname}";
    lle_template_t *parsed = lle_template_parse(template);
    
    if (!parsed) return false;
    if (parsed->node_count != 3) return false;  // username, @, hostname
    
    lle_template_free(parsed);
    return true;
}

/**
 * Test conditional template
 */
static bool test_template_conditional(void) {
    lle_prompt_context_t ctx = {0};
    ctx.last_exit_code = 1;
    
    const char *template = "${?status:ERROR:OK}";
    char *output = NULL;
    
    lle_template_render(template, NULL, 0, &output);
    
    bool passed = (output && strcmp(output, "ERROR") == 0);
    free(output);
    
    return passed;
}

/**
 * Test cache invalidation on chpwd
 */
static bool test_cache_invalidation(void) {
    lle_prompt_context_t ctx = {0};
    lle_prompt_cache_init(&ctx.cache, 64);
    
    // Add a git cache entry
    lle_prompt_cache_entry_t entry = {
        .key = "git_status",
        .tags = (char*[]){"git", NULL},
        .tag_count = 1,
    };
    lle_prompt_cache_set(&ctx.cache, &entry);
    
    // Verify it's there
    if (!lle_prompt_cache_get(&ctx.cache, "git_status")) {
        return false;
    }
    
    // Invalidate by tag
    lle_prompt_cache_invalidate_by_tag(&ctx.cache, "git");
    
    // Verify it's gone
    if (lle_prompt_cache_get(&ctx.cache, "git_status")) {
        return false;
    }
    
    return true;
}

/**
 * Test theme inheritance
 */
static bool test_theme_inheritance(void) {
    lle_prompt_context_t ctx = {0};
    
    // Register parent theme
    lle_theme_definition_t parent = {
        .name = "parent",
        .symbols = {.prompt = "$"},
    };
    lle_theme_register(&ctx, &parent);
    
    // Register child theme
    lle_theme_definition_t child = {
        .name = "child",
        .parent = "parent",
        .symbols = {.continuation = ">"},
    };
    lle_theme_register(&ctx, &child);
    
    // Resolve child - should inherit prompt from parent
    lle_theme_definition_t *resolved = lle_theme_get_resolved(&ctx, "child");
    
    if (!resolved) return false;
    if (strcmp(resolved->symbols.prompt, "$") != 0) return false;
    if (strcmp(resolved->symbols.continuation, ">") != 0) return false;
    
    return true;
}

/**
 * All test cases
 */
static const lle_prompt_test_case_t all_tests[] = {
    {"template_parse_simple", test_template_parse_simple, 
     "Parse simple template with segments"},
    {"template_conditional", test_template_conditional,
     "Parse and evaluate conditional template"},
    {"cache_invalidation", test_cache_invalidation,
     "Cache invalidation by tag"},
    {"theme_inheritance", test_theme_inheritance,
     "Theme property inheritance"},
};

/**
 * Run all tests
 */
void lle_prompt_run_all_tests(void) {
    size_t passed = 0;
    size_t total = sizeof(all_tests) / sizeof(all_tests[0]);
    
    for (size_t i = 0; i < total; i++) {
        lle_prompt_test_result_t result = lle_prompt_run_test(&all_tests[i]);
        
        if (result.passed) {
            printf("✓ %s (%lu ns)\n", result.test_name, result.duration_ns);
            passed++;
        } else {
            printf("✗ %s: %s\n", result.test_name, 
                   result.error_message ?: "failed");
        }
    }
    
    printf("\n%zu/%zu tests passed\n", passed, total);
}
```

---

## 17. Migration Path

### 17.1 Compatibility with Current System

```c
/**
 * Legacy theme adapter
 * Converts old theme_definition_t to new format
 */
lle_theme_definition_t *lle_theme_from_legacy(
    const theme_definition_t *legacy
) {
    if (!legacy) return NULL;
    
    lle_theme_definition_t *theme = calloc(1, sizeof(*theme));
    if (!theme) return NULL;
    
    strncpy(theme->name, legacy->name, sizeof(theme->name) - 1);
    
    // Convert template format from %{} to ${}
    theme->templates.ps1 = lle_template_convert_legacy(legacy->prompt_template);
    theme->templates.ps2 = strdup("> ");
    
    // Copy colors
    // (Color format should be compatible)
    
    return theme;
}

/**
 * Convert legacy template syntax
 * %{user} -> ${username}
 * %{host} -> ${hostname}
 * etc.
 */
char *lle_template_convert_legacy(const char *legacy) {
    if (!legacy) return NULL;
    
    // Map of old to new names
    static const struct {
        const char *old;
        const char *new;
    } mappings[] = {
        {"%{user}", "${username}"},
        {"%{host}", "${hostname}"},
        {"%{cwd}", "${directory}"},
        {"%{git}", "${git}"},
        {"%{time}", "${time}"},
        {"%{jobs}", "${jobs}"},
        {NULL, NULL},
    };
    
    char *result = strdup(legacy);
    
    for (size_t i = 0; mappings[i].old; i++) {
        // Replace all occurrences
        char *pos;
        while ((pos = strstr(result, mappings[i].old))) {
            size_t old_len = strlen(mappings[i].old);
            size_t new_len = strlen(mappings[i].new);
            size_t result_len = strlen(result);
            
            char *new_result = malloc(result_len - old_len + new_len + 1);
            memcpy(new_result, result, pos - result);
            memcpy(new_result + (pos - result), mappings[i].new, new_len);
            strcpy(new_result + (pos - result) + new_len, pos + old_len);
            
            free(result);
            result = new_result;
        }
    }
    
    return result;
}
```

### 17.2 Deprecation Warnings

```c
/**
 * Check for deprecated usage and warn
 */
void lle_prompt_check_deprecations(lle_prompt_context_t *ctx) {
    // Check for old environment variables
    if (getenv("LUSH_THEME")) {
        lle_log(LLE_LOG_WARN, "prompt",
                "$LUSH_THEME is deprecated, use $LUSH_PROMPT");
    }
    
    // Check for old template syntax in config
    if (ctx->config.ps1_override && 
        strstr(ctx->config.ps1_override, "%{")) {
        lle_log(LLE_LOG_WARN, "prompt",
                "Legacy %%{} template syntax detected, "
                "please migrate to ${} syntax");
    }
}
```

---

## 18. API Summary

### 18.1 Public API

```c
// Initialization
lle_prompt_result_t lle_prompt_init(lle_prompt_context_t *ctx);
void lle_prompt_cleanup(lle_prompt_context_t *ctx);

// Configuration
lle_prompt_result_t lle_prompt_config_load(lle_prompt_config_t *config);
lle_prompt_result_t lle_prompt_set_theme(lle_prompt_context_t *ctx, 
                                          const char *name);

// Theme management
lle_prompt_result_t lle_theme_register(lle_prompt_context_t *ctx,
                                        const lle_theme_definition_t *theme);
const lle_theme_definition_t *lle_theme_get(lle_prompt_context_t *ctx,
                                             const char *name);

// Segment management  
lle_prompt_result_t lle_segment_register(lle_prompt_context_t *ctx,
                                          const lle_segment_definition_t *seg);

// Prompt generation
lle_prompt_result_t lle_prompt_render(lle_prompt_context_t *ctx,
                                       char **output);
lle_prompt_result_t lle_prompt_render_safe(lle_prompt_context_t *ctx,
                                            char **output);

// PS1/PS2/RPROMPT access
const char *lle_prompt_get_ps1(lle_prompt_context_t *ctx);
const char *lle_prompt_get_ps2(lle_prompt_context_t *ctx);
const char *lle_prompt_get_rprompt(lle_prompt_context_t *ctx);

// Event handling
lle_prompt_result_t lle_prompt_event_subscribe(lle_prompt_context_t *ctx,
                                                lle_prompt_event_t event,
                                                lle_prompt_event_callback_t cb,
                                                void *user_data,
                                                int priority);

// Cache control
void lle_prompt_cache_invalidate(lle_prompt_context_t *ctx);
void lle_prompt_cache_invalidate_by_tag(lle_prompt_cache_t *cache,
                                         const char *tag);

// Transient prompt
lle_prompt_result_t lle_prompt_apply_transient(lle_prompt_context_t *ctx);

// Performance
void lle_prompt_get_perf_stats(lle_prompt_context_t *ctx,
                                lle_prompt_perf_stats_t *stats);
```

### 18.2 Header File Structure

```
include/
  prompt/
    prompt.h           # Main public API
    prompt_types.h     # Type definitions
    prompt_theme.h     # Theme-specific API
    prompt_segment.h   # Segment API
    prompt_template.h  # Template engine API
    prompt_config.h    # Configuration API
    prompt_internal.h  # Internal use only
```

---

## 19. Implementation Phases

### Phase 1: Core Infrastructure
- Type definitions and result codes
- Basic prompt context initialization
- Simple template engine (segment substitution only)
- Built-in segments (username, hostname, directory)
- Default theme

### Phase 2: Cache and Events
- Cache system with TTL
- Shell event hub integration (Spec 26)
- Event-driven invalidation (LLE_SHELL_EVENT_DIRECTORY_CHANGED)
- Tag-based cache invalidation
- Git segment with caching

### Phase 3: Async System
- Worker thread pool
- Async git status
- Async completion callbacks
- Request queue management

### Phase 4: Advanced Features
- Theme inheritance
- Conditional templates
- Transient prompt
- Configuration file parsing (TOML)

### Phase 5: Polish
- External prompt integration
- Performance optimization
- Symbol set detection
- Full test suite
- Documentation

---

## 20. References

- Starship: https://starship.rs/
- Oh-My-Posh: https://ohmyposh.dev/
- Powerlevel10k: https://github.com/romkatv/powerlevel10k
- Issue #16: Stale git prompt after directory change
- LLE Spec 10: Display Controller
- LLE Spec 15: Screen Buffer

