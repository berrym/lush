# Advanced Prompt Management and Widget Hooks Complete Specification

**Document**: 24_advanced_prompt_widget_hooks_complete.md  
**Version**: 1.0.0  
**Date**: 2025-10-10  
**Status**: Implementation-Ready Specification  
**Classification**: Critical Core User Interface Component  

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Widget Lifecycle Hooks System](#3-widget-lifecycle-hooks-system)
4. [Prompt State Management](#4-prompt-state-management)
5. [Terminal Positioning Control](#5-terminal-positioning-control)
6. [Historical Prompt Modification](#6-historical-prompt-modification)
7. [Bottom-Prompt Implementation](#7-bottom-prompt-implementation)
8. [Integration with Display System](#8-integration-with-display-system)
9. [Performance and Responsiveness](#9-performance-and-responsiveness)
10. [Theme and Style Integration](#10-theme-and-style-integration)
11. [Configuration and User Preferences](#11-configuration-and-user-preferences)
12. [Event System Integration](#12-event-system-integration)
13. [Memory Management](#13-memory-management)
14. [Error Handling and Recovery](#14-error-handling-and-recovery)
15. [Testing and Validation](#15-testing-and-validation)
16. [Implementation Roadmap](#16-implementation-roadmap)

---

## 1. Executive Summary

### 1.1 Purpose

The Advanced Prompt Management and Widget Hooks System provides sophisticated prompt control capabilities enabling advanced features like bottom-anchored prompts, historical prompt modification, and comprehensive widget lifecycle management. This system implements ZSH-equivalent functionality such as `zle-line-init` and `precmd` hooks while extending capabilities for modern terminal interfaces.

### 1.2 Key Features

- **Widget Lifecycle Hooks**: Complete ZSH-equivalent hook system (`zle-line-init`, `precmd`, `preexec`, etc.)
- **Bottom-Prompt Anchoring**: Always render active prompt at terminal bottom with historical prompt simplification
- **Prompt State Management**: Multiple prompt states (full-featured, simplified, custom) with seamless transitions
- **Terminal Positioning Control**: Precise cursor and content positioning within terminal viewport
- **Historical Prompt Modification**: Retroactive modification of previously rendered prompts
- **Dynamic Geometry Management**: Automatic adaptation to terminal resizing and content changes
- **Performance Excellence**: Sub-1ms prompt repositioning with intelligent caching
- **Theme Integration**: Full compatibility with all Lush themes and styling systems

### 1.3 Critical Design Requirements

1. **First-Class Feature Status**: Advanced prompt management available as built-in capability
2. **ZSH Compatibility**: All common ZSH prompt management patterns supported
3. **Performance Excellence**: Invisible to user - no noticeable delay or flicker
4. **Universal Terminal Support**: Works consistently across all terminal types
5. **Complete Customization**: Full widget system integration for unlimited extensibility
6. **Theme Preservation**: All prompt features (git info, themes, etc.) preserved on active prompt

---

## 2. Architecture Overview

### 2.1 Core Component Structure

```c
// Primary prompt management and widget hooks system
typedef struct lle_prompt_management_system {
    // Core components
    lle_widget_hooks_manager_t *hooks_manager;         // Widget lifecycle hooks
    lle_prompt_state_manager_t *state_manager;         // Prompt state management
    lle_terminal_position_controller_t *position_ctrl; // Terminal positioning
    lle_prompt_history_manager_t *history_manager;     // Historical prompt management
    lle_geometry_manager_t *geometry_manager;          // Terminal geometry tracking
    
    // Bottom-prompt specific
    lle_bottom_prompt_controller_t *bottom_controller;  // Bottom-prompt implementation
    lle_prompt_simplifier_t *prompt_simplifier;        // Historical prompt simplification
    lle_cursor_manager_t *cursor_manager;              // Cursor positioning control
    
    // Integration systems
    lle_display_controller_t *display_controller;      // Display system integration
    lle_theme_integration_t *theme_integration;        // Theme system integration
    lle_event_system_t *event_system;                  // Event system integration
    
    // Performance optimization
    lle_prompt_cache_t *prompt_cache;                  // Prompt rendering cache
    lle_position_cache_t *position_cache;              // Position calculation cache
    lle_geometry_cache_t *geometry_cache;              // Terminal geometry cache
    
    // Configuration and state
    lle_prompt_config_t *config;                       // Prompt management configuration
    lle_prompt_session_t *current_session;             // Current editing session
    lle_memory_pool_t *prompt_memory_pool;             // Prompt-specific memory pool
    
    // Synchronization
    pthread_rwlock_t system_lock;                      // Thread-safe access
    bool system_active;                                 // System operational state
    uint64_t session_counter;                           // Session tracking counter
} lle_prompt_management_system_t;
```

### 2.2 Widget Hook Types

```c
// Comprehensive widget lifecycle hook types (ZSH-equivalent)
typedef enum lle_widget_hook_type {
    // Line editor lifecycle hooks
    LLE_HOOK_ZLE_LINE_INIT,                            // Before line editing begins (zle-line-init)
    LLE_HOOK_ZLE_LINE_FINISH,                          // After line editing completes (zle-line-finish)
    LLE_HOOK_ZLE_KEYMAP_SELECT,                        // When keymap changes (zle-keymap-select)
    
    // Command execution hooks  
    LLE_HOOK_PRECMD,                                   // Before command prompt display (precmd)
    LLE_HOOK_PREEXEC,                                  // Before command execution (preexec)
    LLE_HOOK_POSTEXEC,                                 // After command execution (postexec)
    LLE_HOOK_CHPWD,                                    // After directory change (chpwd)
    
    // Prompt rendering hooks
    LLE_HOOK_PROMPT_PRE_RENDER,                        // Before prompt rendering
    LLE_HOOK_PROMPT_POST_RENDER,                       // After prompt rendering
    LLE_HOOK_PROMPT_POSITION_UPDATE,                   // When prompt position changes
    LLE_HOOK_PROMPT_STATE_CHANGE,                      // When prompt state changes
    
    // Terminal interaction hooks
    LLE_HOOK_TERMINAL_RESIZE,                          // When terminal resizes
    LLE_HOOK_TERMINAL_FOCUS_IN,                        // When terminal gains focus
    LLE_HOOK_TERMINAL_FOCUS_OUT,                       // When terminal loses focus
    LLE_HOOK_TERMINAL_SCROLL,                          // When terminal scrolls
    
    // History hooks
    LLE_HOOK_HISTORY_ADD,                              // When history entry added
    LLE_HOOK_HISTORY_RECALL,                           // When history recalled
    LLE_HOOK_HISTORY_SEARCH,                           // During history search
    
    // Special hooks
    LLE_HOOK_INTERRUPT,                                // When interrupt signal received
    LLE_HOOK_SUSPEND,                                  // When suspend signal received
    LLE_HOOK_CONTINUE,                                 // When continue signal received
    
    // Custom hooks
    LLE_HOOK_CUSTOM_BEGIN = 1000,                      // Start of custom hook range
    LLE_HOOK_CUSTOM_MAX = 9999                         // End of custom hook range
} lle_widget_hook_type_t;
```

---

## 3. Widget Lifecycle Hooks System

### 3.1 Hook Manager Architecture

```c
// Advanced widget hooks manager with comprehensive lifecycle support
typedef struct lle_widget_hooks_manager {
    // Hook registration and management
    lle_hash_table_t *hook_registry;                   // Hook type -> handler list mapping
    lle_hook_priority_queue_t *execution_queue;        // Priority-ordered execution queue
    lle_hook_execution_context_t *execution_context;   // Current execution context
    
    // Hook execution control
    lle_execution_scheduler_t *scheduler;              // Hook execution scheduling
    lle_async_executor_t *async_executor;              // Asynchronous hook execution
    lle_hook_timeout_manager_t *timeout_manager;       // Hook timeout management
    
    // Performance monitoring
    lle_hook_performance_tracker_t *perf_tracker;      // Hook performance tracking
    lle_execution_metrics_t *metrics;                  // Execution metrics
    
    // Error handling
    lle_hook_error_handler_t *error_handler;           // Hook error handling
    lle_recovery_manager_t *recovery_manager;          // Error recovery management
    
    // Memory management
    lle_memory_pool_t *hooks_memory_pool;              // Hooks-specific memory pool
    
    // Configuration
    lle_hooks_config_t *config;                        // Hook system configuration
    bool enable_async_execution;                       // Async execution enabled
    bool enable_timeout_protection;                    // Timeout protection enabled
    uint32_t max_hook_execution_time_ms;               // Maximum hook execution time
} lle_widget_hooks_manager_t;

// Hook handler function signature with comprehensive context
typedef lle_result_t (*lle_hook_handler_fn)(lle_hook_execution_context_t *context);

// Hook registration structure
typedef struct lle_hook_registration {
    lle_widget_hook_type_t hook_type;                  // Type of hook
    lle_hook_handler_fn handler_function;              // Handler function
    void *user_data;                                   // User-provided context data
    lle_hook_priority_t priority;                      // Execution priority
    char *hook_name;                                   // Human-readable name
    char *description;                                 // Hook description
    
    // Execution control
    bool async_capable;                                // Can execute asynchronously
    bool blocking_allowed;                             // Can block execution
    uint32_t timeout_ms;                               // Timeout for this hook
    
    // Registration metadata
    uint64_t registration_id;                          // Unique registration ID
    struct timespec registration_time;                 // Registration timestamp
    uint64_t execution_count;                          // Number of times executed
    bool enabled;                                      // Hook enabled flag
} lle_hook_registration_t;
```

### 3.2 ZLE-Line-Init Hook Implementation

```c
// Implementation of zle-line-init equivalent for line editing initialization
lle_result_t lle_execute_zle_line_init_hooks(
    lle_widget_hooks_manager_t *hooks_mgr,
    lle_prompt_management_system_t *prompt_system,
    lle_editor_t *editor) {
    
    if (!hooks_mgr || !prompt_system || !editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Step 1: Create execution context for zle-line-init hooks
    lle_hook_execution_context_t *context = lle_hook_context_create(
        hooks_mgr->hooks_memory_pool);
    
    if (!context) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Step 2: Set up context with editor and prompt system state
    context->hook_type = LLE_HOOK_ZLE_LINE_INIT;
    context->editor = editor;
    context->prompt_system = prompt_system;
    context->buffer = editor->buffer;
    context->display_controller = editor->display_controller;
    context->event_system = editor->event_system;
    context->session_id = prompt_system->session_counter;
    
    // Step 3: Capture current prompt state for restoration
    lle_prompt_state_t *current_state = NULL;
    lle_result_t result = lle_prompt_state_manager_capture_current_state(
        prompt_system->state_manager, &current_state);
    
    if (result == LLE_SUCCESS && current_state) {
        context->original_prompt_state = current_state;
    }
    
    // Step 4: Execute all registered zle-line-init hooks in priority order
    result = lle_execute_hooks_for_type(hooks_mgr, LLE_HOOK_ZLE_LINE_INIT, context);
    
    // Step 5: Handle hook execution results
    if (result == LLE_SUCCESS) {
        // Check if any hook modified prompt positioning
        if (context->prompt_position_modified) {
            result = lle_apply_prompt_position_changes(prompt_system, context);
        }
        
        // Check if any hook modified prompt state
        if (context->prompt_state_modified) {
            result = lle_apply_prompt_state_changes(prompt_system, context);
        }
        
        // Handle special results from hooks
        switch (context->final_result) {
            case LLE_HOOK_RESULT_ACCEPT_LINE:
                // Hook requested immediate line acceptance
                result = lle_editor_accept_line(editor);
                break;
                
            case LLE_HOOK_RESULT_CANCEL_EDIT:
                // Hook requested edit cancellation
                result = lle_editor_cancel_line(editor);
                break;
                
            case LLE_HOOK_RESULT_RECURSIVE_EDIT:
                // Hook requested recursive editing (like zsh .recursive-edit)
                result = lle_editor_enter_recursive_edit(editor, context);
                break;
                
            case LLE_HOOK_RESULT_CONTINUE:
            default:
                // Normal continuation
                break;
        }
    }
    
    // Step 6: Update performance metrics
    if (hooks_mgr->perf_tracker) {
        lle_hook_performance_tracker_record_execution(
            hooks_mgr->perf_tracker, LLE_HOOK_ZLE_LINE_INIT, 
            context->execution_start_time, context->execution_end_time,
            context->hooks_executed_count);
    }
    
    // Step 7: Cleanup execution context
    lle_hook_context_destroy(context);
    
    return result;
}
```

---

## 4. Prompt State Management

### 4.1 Prompt State System

```c
// Comprehensive prompt state management system
typedef struct lle_prompt_state_manager {
    // State storage and management
    lle_hash_table_t *named_states;                    // Named prompt states
    lle_prompt_state_t *active_state;                  // Currently active state
    lle_prompt_state_t *default_state;                 // Default prompt state
    lle_prompt_state_stack_t *state_stack;             // State stack for nesting
    
    // State transition management
    lle_transition_controller_t *transition_ctrl;      // State transition control
    lle_animation_manager_t *animation_manager;        // Transition animations
    
    // Performance optimization
    lle_state_cache_t *state_cache;                    // Rendered state cache
    lle_diff_calculator_t *diff_calculator;            // State difference calculation
    
    // Memory management
    lle_memory_pool_t *state_memory_pool;              // State-specific memory pool
    
    // Configuration
    lle_state_config_t *config;                        // State system configuration
} lle_prompt_state_manager_t;

// Comprehensive prompt state structure
typedef struct lle_prompt_state {
    // State identification
    char *state_name;                                   // Unique state name
    char *description;                                  // State description
    lle_prompt_state_type_t state_type;                // State type
    
    // Prompt content
    char *primary_prompt;                               // Primary prompt (PS1)
    char *secondary_prompt;                             // Secondary prompt (PS2)
    char *right_prompt;                                 // Right-side prompt (RPS1)
    char *continuation_prompt;                          // Continuation prompt
    
    // Visual properties
    lle_prompt_colors_t *colors;                       // Color scheme
    lle_prompt_symbols_t *symbols;                     // Symbol set
    lle_prompt_layout_t *layout;                       // Layout configuration
    lle_theme_info_t *theme_info;                      // Theme information
    
    // Positioning and geometry
    lle_prompt_position_t position;                    // Prompt position
    lle_prompt_geometry_t geometry;                    // Prompt geometry
    bool bottom_anchored;                               // Bottom-anchored flag
    bool historical_simplified;                        // Historical simplification flag
    
    // Content generation
    lle_prompt_generator_fn generator_function;        // Dynamic content generator
    void *generator_context;                           // Generator context data
    struct timespec last_generated;                    // Last generation time
    bool needs_regeneration;                           // Regeneration required flag
    
    // Caching and optimization
    char *cached_rendered_content;                      // Cached rendered content
    size_t cached_content_length;                       // Cache content length
    uint64_t cache_version;                            // Cache version number
    bool cache_valid;                                  // Cache validity flag
    
    // Metadata
    struct timespec creation_time;                      // State creation time
    struct timespec last_used;                         // Last usage time
    uint64_t usage_count;                              // Usage frequency
    bool is_temporary;                                 // Temporary state flag
} lle_prompt_state_t;

// Prompt state types
typedef enum lle_prompt_state_type {
    LLE_PROMPT_STATE_FULL_FEATURED,                    // Full-featured prompt with all info
    LLE_PROMPT_STATE_SIMPLIFIED,                       // Simplified prompt for history
    LLE_PROMPT_STATE_MINIMAL,                          // Minimal prompt (just indicator)
    LLE_PROMPT_STATE_CUSTOM,                           // Custom user-defined state
    LLE_PROMPT_STATE_BOTTOM_ACTIVE,                    // Bottom-anchored active prompt
    LLE_PROMPT_STATE_HISTORICAL,                       // Historical prompt state
    LLE_PROMPT_STATE_LOADING,                          // Loading/waiting state
    LLE_PROMPT_STATE_ERROR                             // Error state
} lle_prompt_state_type_t;
```

### 4.2 State Transition Management

```c
// Seamless state transitions with animation and optimization
lle_result_t lle_prompt_state_manager_transition_to_state(
    lle_prompt_state_manager_t *state_mgr,
    const char *target_state_name,
    lle_transition_options_t *options) {
    
    if (!state_mgr || !target_state_name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Step 1: Resolve target state
    lle_prompt_state_t *target_state = lle_hash_table_get(
        state_mgr->named_states, target_state_name, strlen(target_state_name));
    
    if (!target_state) {
        return LLE_ERROR_STATE_NOT_FOUND;
    }
    
    // Step 2: Check if already in target state
    if (state_mgr->active_state == target_state) {
        return LLE_SUCCESS; // Already in desired state
    }
    
    // Step 3: Validate transition is allowed
    lle_result_t validation_result = lle_validate_state_transition(
        state_mgr, state_mgr->active_state, target_state);
    
    if (validation_result != LLE_SUCCESS) {
        return validation_result;
    }
    
    // Step 4: Create transition context
    lle_state_transition_t *transition = lle_state_transition_create(
        state_mgr->state_memory_pool,
        state_mgr->active_state,
        target_state,
        options);
    
    if (!transition) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Step 5: Calculate state differences for optimization
    lle_result_t diff_result = lle_diff_calculator_calculate_state_diff(
        state_mgr->diff_calculator,
        state_mgr->active_state,
        target_state,
        &transition->state_diff);
    
    // Step 6: Execute pre-transition hooks
    lle_result_t hook_result = lle_execute_state_transition_hooks(
        state_mgr, LLE_HOOK_PROMPT_STATE_CHANGE, transition);
    
    if (hook_result != LLE_SUCCESS) {
        lle_state_transition_destroy(transition);
        return hook_result;
    }
    
    // Step 7: Perform atomic state switch
    lle_prompt_state_t *previous_state = state_mgr->active_state;
    state_mgr->active_state = target_state;
    
    // Step 8: Update display if needed
    lle_result_t result = LLE_SUCCESS;
    if (transition->requires_display_update) {
        result = lle_apply_prompt_state_to_display(state_mgr, target_state, transition);
    }
    
    // Step 9: Update state stack if requested
    if (options && options->push_to_stack) {
        lle_prompt_state_stack_push(state_mgr->state_stack, previous_state);
    }
    
    // Step 10: Execute post-transition hooks
    if (result == LLE_SUCCESS) {
        lle_execute_state_transition_hooks(
            state_mgr, LLE_HOOK_PROMPT_POST_RENDER, transition);
    }
    
    // Step 11: Update usage statistics
    target_state->usage_count++;
    clock_gettime(CLOCK_MONOTONIC, &target_state->last_used);
    
    // Step 12: Cleanup transition
    lle_state_transition_destroy(transition);
    
    return result;
}
```

---

## 5. Terminal Positioning Control

### 5.1 Terminal Position Controller

```c
// Advanced terminal positioning with precise control
typedef struct lle_terminal_position_controller {
    // Terminal geometry tracking
    lle_terminal_geometry_t *current_geometry;         // Current terminal size
    lle_geometry_change_detector_t *change_detector;   // Geometry change detection
    lle_viewport_manager_t *viewport_manager;          // Viewport management
    
    // Cursor management
    lle_cursor_position_tracker_t *cursor_tracker;     // Cursor position tracking
    lle_cursor_movement_optimizer_t *movement_optimizer; // Cursor movement optimization
    
    // Content positioning
    lle_content_positioner_t *content_positioner;      // Content positioning engine
    lle_scroll_manager_t *scroll_manager;              // Scroll management
    lle_line_manager_t *line_manager;                  // Line-based positioning
    
    // Performance optimization
    lle_position_cache_t *position_cache;              // Position calculation cache
    lle_movement_cache_t *movement_cache;              // Movement sequence cache
    
    // Terminal capability detection
    lle_terminal_capabilities_t *capabilities;         // Terminal capabilities
    lle_positioning_support_t *positioning_support;    // Positioning feature support
    
    // Memory management
    lle_memory_pool_t *position_memory_pool;           // Positioning memory pool
    
    // Configuration
    lle_positioning_config_t *config;                  // Positioning configuration
} lle_terminal_position_controller_t;

// Terminal geometry tracking with real-time updates
typedef struct lle_terminal_geometry {
    // Current dimensions
    size_t width;                                       // Terminal width in columns
    size_t height;                                      // Terminal height in rows
    size_t pixel_width;                                 // Width in pixels (if available)
    size_t pixel_height;                               // Height in pixels (if available)
    
    // Character dimensions
    size_t char_width;                                  // Character width in pixels
    size_t char_height;                                 // Character height in pixels
    
    // Viewport information
    size_t scroll_top;                                  // Top of scrollable region
    size_t scroll_bottom;                               // Bottom of scrollable region
    size_t visible_lines;                               // Visible lines in viewport
    
    // Margins and padding
    size_t top_margin;                                  // Top margin
    size_t bottom_margin;                               // Bottom margin
    size_t left_margin;                                 // Left margin
    size_t right_margin;                                // Right margin
    
    // Change tracking
    struct timespec last_updated;                       // Last geometry update
    bool geometry_changed;                              // Geometry change flag
    lle_geometry_change_type_t change_type;             // Type of change
    
    // Capabilities
    bool supports_cursor_positioning;                   // Cursor positioning support
    bool supports_scrolling_regions;                    // Scrolling region support
    bool supports_alternate_screen;                     // Alternate screen support
} lle_terminal_geometry_t;

// Position cursor at specific terminal coordinates with optimization
lle_result_t lle_position_cursor_absolute(
    lle_terminal_position_controller_t *pos_ctrl,
    size_t row, size_t column) {
    
    if (!pos_ctrl) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Step 1: Validate coordinates
    if (row >= pos_ctrl->current_geometry->height || 
        column >= pos_ctrl->current_geometry->width) {
        return LLE_ERROR_INVALID_COORDINATES;
    }
    
    // Step 2: Check if already at target position
    lle_cursor_position_t current_pos;
    lle_result_t result = lle_cursor_tracker_get_current_position(
        pos_ctrl->cursor_tracker, &current_pos);
    
    if (result == LLE_SUCCESS && 
        current_pos.row == row && current_pos.column == column) {
        return LLE_SUCCESS; // Already at target position
    }
    
    // Step 3: Calculate optimal movement sequence
    lle_movement_sequence_t *movement = NULL;
    result = lle_cursor_movement_optimizer_calculate_movement(
        pos_ctrl->movement_optimizer,
        &current_pos,
        &(lle_cursor_position_t){.row = row, .column = column},
        &movement);
    
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Step 4: Execute movement sequence
    result = lle_execute_cursor_movement_sequence(pos_ctrl, movement);
    
    // Step 5: Update cursor position tracking
    if (result == LLE_SUCCESS) {
        lle_cursor_position_t new_pos = {.row = row, .column = column};
        lle_cursor_tracker_update_position(pos_ctrl->cursor_tracker, &new_pos);
    }
    
    // Step 6: Cleanup
    lle_movement_sequence_destroy(movement);
    
    return result;
}

// Move cursor to bottom line of terminal (for bottom-prompt implementation)
lle_result_t lle_position_cursor_bottom_line(
    lle_terminal_position_controller_t *pos_ctrl,
    size_t column) {
    
    if (!pos_ctrl) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Calculate bottom line position
    size_t bottom_line = pos_ctrl->current_geometry->height - 
                        pos_ctrl->current_geometry->bottom_margin - 1;
    
    // Constrain column to terminal width
    if (column >= pos_ctrl->current_geometry->width) {
        column = pos_ctrl->current_geometry->width - 1;
    }
    
    return lle_position_cursor_absolute(pos_ctrl, bottom_line, column);
}
```

---

## 6. Historical Prompt Modification

### 6.1 Prompt History Manager

```c
// Advanced historical prompt modification and management
typedef struct lle_prompt_history_manager {
    // Historical prompt tracking
    lle_circular_buffer_t *prompt_history;             // Historical prompts buffer
    lle_hash_table_t *prompt_line_mapping;             // Prompt to line mapping
    lle_modification_tracker_t *mod_tracker;           // Modification tracking
    
    // Simplification engine
    lle_prompt_simplifier_t *simplifier;               // Prompt simplification
    lle_simplification_rules_t *rules;                 // Simplification rules
    lle_pattern_matcher_t *pattern_matcher;            // Pattern-based simplification
    
    // Display integration
    lle_display_updater_t *display_updater;            // Display update coordination
    lle_render_manager_t *render_manager;              // Rendering management
    
    // Performance optimization
    lle_modification_cache_t *mod_cache;               // Modification cache
    lle_batch_processor_t *batch_processor;           // Batch modification processing
    
    // Memory management
    lle_memory_pool_t *history_memory_pool;            // History-specific memory pool
    
    // Configuration
    lle_history_config_t *config;                      // History system configuration
    size_t max_history_entries;                        // Maximum history entries
    bool enable_batch_modifications;                   // Batch processing enabled
} lle_prompt_history_manager_t;

// Historical prompt entry with modification capabilities
typedef struct lle_historical_prompt_entry {
    // Original prompt information
    char *original_prompt;                              // Original full prompt
    char *simplified_prompt;                            // Simplified version
    size_t terminal_line;                               // Terminal line number
    size_t prompt_length;                               // Original prompt length
    
    // State information
    lle_prompt_state_t *original_state;                // Original prompt state
    lle_prompt_state_t *current_state;                 // Current prompt state
    bool is_simplified;                                 // Simplification status
    
    // Modification tracking
    struct timespec creation_time;                      // Entry creation time
    struct timespec last_modified;                      // Last modification time
    uint32_t modification_count;                        // Number of modifications
    
    // Display information
    lle_display_coordinates_t display_coords;           // Display coordinates
    lle_render_info_t *render_info;                     // Rendering information
    bool visible_on_screen;                             // Visibility status
    
    // Memory management
    bool owns_prompt_memory;                            // Memory ownership flag
} lle_historical_prompt_entry_t;

// Simplify historical prompts to basic indicator (like > character)
lle_result_t lle_simplify_historical_prompts(
    lle_prompt_history_manager_t *history_mgr,
    const char *simple_indicator,
    lle_simplification_options_t *options) {
    
    if (!history_mgr || !simple_indicator) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Step 1: Get list of historical prompt entries
    lle_historical_prompt_entry_t **entries = NULL;
    size_t entry_count = 0;
    
    lle_result_t result = lle_prompt_history_get_modifiable_entries(
        history_mgr, &entries, &entry_count);
    
    if (result != LLE_SUCCESS || entry_count == 0) {
        return result;
    }
    
    // Step 2: Create batch modification context
    lle_batch_modification_t *batch_mod = NULL;
    if (history_mgr->config->enable_batch_modifications) {
        result = lle_batch_processor_create_batch(
            history_mgr->batch_processor, &batch_mod);
        
        if (result != LLE_SUCCESS) {
            return result;
        }
    }
    
    // Step 3: Process each historical prompt entry
    size_t modified_count = 0;
    for (size_t i = 0; i < entry_count; i++) {
        lle_historical_prompt_entry_t *entry = entries[i];
        
        // Skip if already simplified or not eligible
        if (entry->is_simplified || 
            !lle_is_prompt_simplifiable(entry, options)) {
            continue;
        }
        
        // Create simplified prompt
        char *simplified = lle_create_simplified_prompt(
            history_mgr->simplifier, entry->original_prompt, simple_indicator);
        
        if (!simplified) {
            continue; // Skip on failure
        }
        
        // Update entry with simplified prompt
        if (batch_mod) {
            lle_batch_modification_add_entry_update(batch_mod, entry, simplified);
        } else {
            result = lle_update_historical_prompt_display(
                history_mgr, entry, simplified);
            if (result == LLE_SUCCESS) {
                modified_count++;
            }
        }
        
        free(simplified);
    }
    
    // Step 4: Execute batch modifications if enabled
    if (batch_mod) {
        result = lle_batch_processor_execute_batch(
            history_mgr->batch_processor, batch_mod);
        if (result == LLE_SUCCESS) {
            modified_count = batch_mod->successful_modifications;
        }
        lle_batch_processor_destroy_batch(batch_mod);
    }
    
    return (modified_count > 0) ? LLE_SUCCESS : LLE_ERROR_NO_MODIFICATIONS_APPLIED;
}
```

---

## 7. Bottom-Prompt Implementation

### 7.1 Bottom-Prompt Controller

```c
// Complete bottom-prompt implementation matching zsh functionality
typedef struct lle_bottom_prompt_controller {
    // State management
    lle_bottom_prompt_state_t *current_state;          // Current bottom-prompt state
    lle_prompt_position_tracker_t *position_tracker;   // Position tracking
    lle_terminal_position_controller_t *pos_ctrl;      // Terminal positioning
    
    // Prompt management
    lle_prompt_state_manager_t *state_manager;         // Prompt state management
    lle_prompt_history_manager_t *history_manager;     // Historical prompt management
    
    // Configuration
    lle_bottom_prompt_config_t *config;                // Bottom-prompt configuration
    char *simplified_indicator;                        // Simplified prompt indicator
    lle_color_t indicator_color;                       // Indicator color
    
    // Performance optimization
    bool enabled;                                       // Bottom-prompt enabled
    bool auto_simplify_history;                        // Auto-simplify historical prompts
    uint32_t max_prompt_history;                       // Maximum prompt history
} lle_bottom_prompt_controller_t;

// Implementation of bottom-prompt functionality (zsh equivalent)
lle_result_t lle_bottom_prompt_zle_line_init_hook(
    lle_hook_execution_context_t *context) {
    
    lle_bottom_prompt_controller_t *bottom_ctrl = 
        (lle_bottom_prompt_controller_t *)context->user_data;
    
    if (!bottom_ctrl || !bottom_ctrl->enabled) {
        return LLE_HOOK_RESULT_CONTINUE;
    }
    
    lle_result_t result = LLE_SUCCESS;
    
    // Step 1: Save current prompt state (equivalent to saved_prompt=$PROMPT)
    lle_prompt_state_t *saved_state = NULL;
    result = lle_prompt_state_manager_capture_current_state(
        bottom_ctrl->state_manager, &saved_state);
    
    if (result != LLE_SUCCESS) {
        return LLE_HOOK_RESULT_ERROR;
    }
    
    // Step 2: Create simplified prompt state
    lle_prompt_state_t *simplified_state = NULL;
    result = lle_create_simplified_prompt_state(
        bottom_ctrl, bottom_ctrl->simplified_indicator, &simplified_state);
    
    if (result != LLE_SUCCESS) {
        lle_prompt_state_destroy(saved_state);
        return LLE_HOOK_RESULT_ERROR;
    }
    
    // Step 3: Apply simplified state to historical prompts
    if (bottom_ctrl->auto_simplify_history) {
        result = lle_simplify_historical_prompts(
            bottom_ctrl->history_manager,
            bottom_ctrl->simplified_indicator,
            NULL);
        // Continue even if simplification fails
    }
    
    // Step 4: Position cursor at bottom of terminal
    result = lle_position_cursor_bottom_line(
        bottom_ctrl->pos_ctrl, 0);
    
    if (result != LLE_SUCCESS) {
        lle_prompt_state_destroy(saved_state);
        lle_prompt_state_destroy(simplified_state);
        return LLE_HOOK_RESULT_ERROR;
    }
    
    // Step 5: Set up recursive edit mode (equivalent to zle .recursive-edit)
    context->enter_recursive_edit = true;
    context->recursive_edit_context = lle_create_recursive_edit_context(
        context->editor, saved_state);
    
    // Step 6: Store states for restoration
    context->original_prompt_state = saved_state;
    context->temporary_prompt_state = simplified_state;
    context->prompt_state_modified = true;
    
    return LLE_HOOK_RESULT_CONTINUE;
}

// Handle bottom-prompt line completion
lle_result_t lle_bottom_prompt_line_finish_hook(
    lle_hook_execution_context_t *context) {
    
    lle_bottom_prompt_controller_t *bottom_ctrl = 
        (lle_bottom_prompt_controller_t *)context->user_data;
    
    if (!bottom_ctrl || !context->original_prompt_state) {
        return LLE_HOOK_RESULT_CONTINUE;
    }
    
    // Step 1: Restore original prompt state
    lle_result_t result = lle_prompt_state_manager_restore_state(
        bottom_ctrl->state_manager, context->original_prompt_state);
    
    // Step 2: Handle line acceptance/cancellation
    if (context->line_accepted) {
        result = lle_editor_accept_line(context->editor);
    } else if (context->line_cancelled) {
        result = lle_editor_cancel_line(context->editor);
    }
    
    // Step 3: Cleanup temporary states
    if (context->temporary_prompt_state) {
        lle_prompt_state_destroy(context->temporary_prompt_state);
    }
    
    return (result == LLE_SUCCESS) ? LLE_HOOK_RESULT_CONTINUE : LLE_HOOK_RESULT_ERROR;
}
```

---

## 8. Integration with Display System

### 8.1 Display Integration

```c
// Seamless integration with Lush display system
lle_result_t lle_prompt_hooks_integrate_with_display(
    lle_prompt_management_system_t *prompt_system,
    lle_display_controller_t *display_controller) {
    
    if (!prompt_system || !display_controller) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Register prompt management as display layer
    lle_display_layer_t prompt_layer = {
        .layer_name = "advanced_prompt_management",
        .layer_priority = LLE_LAYER_PRIORITY_PROMPT_MANAGEMENT,
        .render_function = lle_prompt_management_render_layer,
        .update_function = lle_prompt_management_update_layer,
        .cleanup_function = lle_prompt_management_cleanup_layer,
        .user_data = prompt_system
    };
    
    lle_result_t result = lle_display_controller_register_layer(
        display_controller, &prompt_layer);
    
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    prompt_system->display_controller = display_controller;
    
    return LLE_SUCCESS;
}
```

---

## 9. Implementation Roadmap

### 9.1 Phase 1: Core Hook System (Priority 1)

**Timeline**: 1-2 weeks  
**Dependencies**: Widget system and display integration

**Deliverables:**
- Widget lifecycle hooks manager
- Basic hook registration and execution
- ZLE-line-init and precmd hook implementations
- Terminal positioning control
- Basic prompt state management

**Success Criteria:**
- ZLE-line-init hooks execute properly
- Terminal cursor positioning works accurately
- Basic prompt state transitions functional
- Performance targets met (<1ms hook execution)

### 9.2 Phase 2: Bottom-Prompt Implementation (Priority 1)

**Timeline**: 1-2 weeks  
**Dependencies**: Phase 1 completion

**Deliverables:**
- Bottom-prompt controller
- Historical prompt modification system
- Prompt simplification engine
- Complete bottom-anchored prompt functionality
- User configuration integration

**Success Criteria:**
- Bottom-prompt works exactly like zsh implementation
- Historical prompts simplified to indicator character
- Full-featured prompt preserved at bottom
- Seamless terminal resizing support
- Zero noticeable performance impact

### 9.3 Phase 3: Advanced Features (Priority 2)

**Timeline**: 1 week  
**Dependencies**: Phase 2 completion

**Deliverables:**
- Advanced hook system features
- Animation and transition effects
- Performance optimizations
- Comprehensive configuration options
- Complete theme integration

**Success Criteria:**
- All performance targets consistently met
- Perfect theme integration across all features
- Comprehensive configuration options available
- Professional-quality user experience

---

## 10. Success Criteria

**Functional Requirements:**
- **Complete ZSH Compatibility**: All common ZSH prompt patterns supported including bottom-prompt
- **Perfect Bottom-Prompt**: Active prompt always at terminal bottom with historical simplification
- **Widget Hook System**: Full lifecycle hook support (zle-line-init, precmd, preexec, etc.)
- **Terminal Positioning**: Precise cursor and content positioning control
- **Prompt State Management**: Multiple prompt states with seamless transitions

**Performance Requirements:**
- **Hook Execution**: <1ms for typical hook operations
- **Cursor Positioning**: <0.5ms for terminal positioning operations
- **Prompt Transitions**: <2ms for state transitions with animation
- **Historical Modification**: <5ms for batch historical prompt simplification
- **Memory Efficiency**: <1MB additional memory usage for prompt management

**Quality Requirements:**
- **Zero Configuration**: Advanced prompt features work perfectly out-of-box
- **Theme Integration**: Full compatibility with all Lush themes and styling
- **Terminal Compatibility**: Works consistently across all terminal types and sizes
- **Professional Quality**: Invisible operation with no flicker or delay

---

## Conclusion

**Advanced Prompt Management and Widget Hooks Implementation Complete:** This specification provides comprehensive implementation of sophisticated prompt management capabilities enabling advanced features like bottom-anchored prompts with historical simplification, matching and extending ZSH functionality within the LLE widget system architecture.

**Key Implementation Benefits:**
- **ZSH Feature Parity**: Complete implementation of bottom-prompt functionality as requested
- **Widget System Integration**: First-class citizen status through comprehensive hook system
- **Performance Excellence**: Sub-1ms hook execution with intelligent caching and optimization
- **Universal Compatibility**: Works seamlessly across all terminal environments
- **Complete Customization**: Full widget system integration for unlimited extensibility

**Critical Functionality Delivered:**
1. **Widget Lifecycle Hooks** - Complete ZSH-equivalent hook system (zle-line-init, precmd, etc.)
2. **Bottom-Prompt Implementation** - Exact functionality matching your zsh configuration
3. **Prompt State Management** - Multiple prompt states with seamless transitions
4. **Terminal Positioning Control** - Precise cursor and content positioning
5. **Historical Prompt Modification** - Retroactive simplification of previous prompts

**Implementation Status**: Complete specification ready for development  
**Next Priority**: Integration with extensibility framework to ensure widget system supports all hook requirements

**This specification directly implements your daily-use bottom-prompt feature as a first-class capability, enabling the sophisticated prompt management you described through the widget system architecture.**