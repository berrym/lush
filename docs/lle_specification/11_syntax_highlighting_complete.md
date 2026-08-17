# Syntax Highlighting Complete Specification

**Document**: 11_syntax_highlighting_complete.md  
**Version**: 2.0.0  
**Date**: 2025-10-11  
**Status**: Integration-Ready Specification (Phase 2 Integration Refactoring)
**Classification**: Critical Core Component

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Widget Hook Integration](#3-widget-hook-integration)
4. [Adaptive Terminal Integration](#4-adaptive-terminal-integration)
5. [Syntax Analysis Engine](#5-syntax-analysis-engine)
6. [Real-Time Highlighting System](#6-real-time-highlighting-system)
7. [Color Management](#7-color-management)
8. [Shell Language Support](#8-shell-language-support)
9. [Display Integration](#9-display-integration)
10. [Performance Optimization](#10-performance-optimization)
11. [Memory Management Integration](#11-memory-management-integration)
12. [Event System Coordination](#12-event-system-coordination)
13. [Configuration Management](#13-configuration-management)
14. [Error Handling and Recovery](#14-error-handling-and-recovery)
15. [Testing and Validation](#15-testing-and-validation)
16. [Implementation Roadmap](#16-implementation-roadmap)

---

## 1. Executive Summary

### 1.1 Purpose

The LLE Syntax Highlighting System provides real-time visual enhancement of shell commands through intelligent syntax analysis and color coding. This system performs live lexical analysis of command input, applying contextually appropriate highlighting to improve readability, reduce errors, and enhance the user experience while maintaining sub-millisecond responsiveness and seamless integration with the Lush layered display architecture.

### 1.2 Key Features

- **Real-Time Syntax Analysis**: Live lexical analysis and highlighting during command typing
- **Widget Hook Integration**: Complete integration with advanced prompt widget hooks for bottom-prompt syntax highlighting
- **Adaptive Terminal Integration**: Dynamic color capabilities with terminal-specific optimization and fallbacks
- **Comprehensive Shell Language Support**: Complete support for bash, zsh, POSIX shell syntax patterns
- **Intelligent Context Recognition**: Context-aware highlighting for strings, variables, operators, keywords
- **Error Detection Integration**: Visual indication of syntax errors and potential issues
- **Performance Excellence**: Sub-millisecond highlighting updates with intelligent caching
- **Display System Integration**: Seamless integration with Lush layered display architecture
- **Theme System Integration**: Full support for Lush theme system with customizable color schemes
- **Memory Pool Integration**: Zero-allocation highlighting operations with efficient memory management
- **Extensible Architecture**: Plugin system for custom syntax rules and language support

### 1.3 Critical Design Principles

1. **Real-Time Responsiveness**: Highlighting must update instantly without input lag
2. **Accuracy First**: Precise syntax analysis with minimal false positives or negatives
3. **Visual Clarity**: Highlighting enhances readability without overwhelming the interface
4. **Resource Efficiency**: Minimal CPU and memory overhead during operation
5. **Context Awareness**: Highlighting adapts to shell context and command structure
6. **Theme Integration**: Consistent with Lush visual design and theme system

---

## 2. Architecture Overview

### 2.1 Syntax Highlighting System Architecture

```c
// Primary syntax highlighting system with real-time analysis capabilities
typedef struct lle_syntax_highlighting_system {
    // Core syntax analysis
    lle_syntax_analyzer_t *syntax_analyzer;           // Central syntax analysis engine
    lle_lexical_parser_t *lexical_parser;             // Real-time lexical parsing system
    lle_token_classifier_t *token_classifier;         // Token type classification engine
    lle_syntax_cache_t *syntax_cache;                 // Intelligent syntax analysis caching
    
    // NEW: Integration systems
    lle_widget_hook_integration_t *widget_integration; // Widget hook integration for bottom-prompt support
    lle_adaptive_terminal_integration_t *terminal_integration; // Adaptive terminal color management
    
    // Language support and rules
    lle_shell_grammar_t *shell_grammar;               // Comprehensive shell language grammar
    lle_keyword_database_t *keyword_db;               // Shell keywords and builtin commands
    lle_operator_matcher_t *operator_matcher;         // Shell operators and special characters
    lle_variable_detector_t *variable_detector;       // Variable and substitution detection
    lle_string_analyzer_t *string_analyzer;           // String literal and quoting analysis
    
    // Color and visual management
    lle_color_manager_t *color_manager;               // Color scheme and palette management
    lle_theme_integration_t *theme_integration;       // Lush theme system integration
    lle_highlight_renderer_t *highlight_renderer;     // Syntax highlighting display renderer
    lle_visual_effects_t *visual_effects;             // Visual effects and animations
    
    // Performance and caching
    lle_syntax_metrics_t *perf_metrics;               // Syntax highlighting performance monitoring
    lle_incremental_parser_t *incremental_parser;     // Incremental parsing for efficiency
    lle_dirty_tracking_t *dirty_tracker;              // Change tracking for selective updates
    
    // Integration and coordination
    lle_display_coordinator_t *display_coordinator;   // Lush display system integration
    lle_event_coordinator_t *event_coordinator;       // Event system coordination
    lle_buffer_integration_t *buffer_integration;     // LLE buffer system integration
    memory_pool_t *memory_pool;                       // Lush memory pool integration
    
    // NEW: Integration coordination state
    lle_widget_highlight_mode_t widget_mode;          // Current widget highlighting mode
    bool bottom_prompt_active;                        // Bottom prompt highlighting state
    bool adaptive_colors_active;                      // Adaptive terminal colors enabled
    uint64_t last_widget_update;                      // Last widget state update timestamp
    
    // Configuration and state
    lle_syntax_config_t *config;                      // Syntax highlighting configuration
    lle_highlight_state_t *current_state;             // Current highlighting state
    lle_hash_table_t *token_cache;                    // Fast token lookup hashtable
    lle_hash_table_t *keyword_cache;                  // Keyword classification cache
    
    // Thread safety for integration
    pthread_rwlock_t widget_integration_lock;         // Widget integration synchronization
    pthread_rwlock_t terminal_integration_lock;       // Terminal integration synchronization
    pthread_rwlock_t highlighting_lock;               // Thread-safe access control
    
    // System status and configuration
    bool system_active;                               // Syntax highlighting system status
    bool real_time_enabled;                           // Real-time highlighting status
} lle_syntax_highlighting_system_t;

// Syntax token classification with comprehensive shell support
typedef enum lle_token_type {
    // Basic token types
    LLE_TOKEN_UNKNOWN = 0,                            // Unknown or unclassified token
    LLE_TOKEN_WHITESPACE = 1,                         // Whitespace and separators
    LLE_TOKEN_WORD = 2,                               // Generic word tokens
    LLE_TOKEN_NUMBER = 3,                             // Numeric literals
    
    // Shell language constructs
    LLE_TOKEN_COMMAND = 10,                           // Command names and executables
    LLE_TOKEN_BUILTIN = 11,                           // Shell builtin commands
    LLE_TOKEN_KEYWORD = 12,                           // Shell keywords (if, for, while, etc.)
    LLE_TOKEN_FUNCTION = 13,                          // Function names and calls
    LLE_TOKEN_ALIAS = 14,                             // Command aliases
    
    // Variables and substitutions
    LLE_TOKEN_VARIABLE = 20,                          // Variable references ($var, ${var})
    LLE_TOKEN_ENV_VARIABLE = 21,                      // Environment variables
    LLE_TOKEN_SPECIAL_VARIABLE = 22,                  // Special variables ($?, $#, $@, etc.)
    LLE_TOKEN_PARAMETER_EXPANSION = 23,               // Parameter expansion constructs
    LLE_TOKEN_COMMAND_SUBSTITUTION = 24,              // Command substitution $() and ``
    LLE_TOKEN_ARITHMETIC_EXPANSION = 25,              // Arithmetic expansion $((expr))
    
    // String literals and quoting
    LLE_TOKEN_STRING_SINGLE = 30,                     // Single-quoted strings
    LLE_TOKEN_STRING_DOUBLE = 31,                     // Double-quoted strings
    LLE_TOKEN_STRING_BACKTICK = 32,                   // Backtick command substitution
    LLE_TOKEN_STRING_ESCAPE = 33,                     // Escape sequences
    LLE_TOKEN_HERE_DOC = 34,                          // Here document literals
    LLE_TOKEN_HERE_STRING = 35,                       // Here string literals
    
    // Operators and special characters
    LLE_TOKEN_OPERATOR = 40,                          // General operators
    LLE_TOKEN_PIPE = 41,                              // Pipe operators (|, ||)
    LLE_TOKEN_REDIRECT = 42,                          // Redirection operators (<, >, >>)
    LLE_TOKEN_LOGICAL = 43,                           // Logical operators (&&, ||)
    LLE_TOKEN_ASSIGNMENT = 44,                        // Assignment operators (=, +=)
    LLE_TOKEN_COMPARISON = 45,                        // Comparison operators (==, !=, <, >)
    LLE_TOKEN_ARITHMETIC = 46,                        // Arithmetic operators (+, -, *, /)
    
    // Control flow and structure
    LLE_TOKEN_CONTROL_IF = 50,                        // if statements
    LLE_TOKEN_CONTROL_THEN = 51,                      // then keyword
    LLE_TOKEN_CONTROL_ELSE = 52,                      // else keyword
    LLE_TOKEN_CONTROL_ELIF = 53,                      // elif keyword
    LLE_TOKEN_CONTROL_FI = 54,                        // fi keyword
    LLE_TOKEN_CONTROL_FOR = 55,                       // for loops
    LLE_TOKEN_CONTROL_WHILE = 56,                     // while loops
    LLE_TOKEN_CONTROL_DO = 57,                        // do keyword
    LLE_TOKEN_CONTROL_DONE = 58,                      // done keyword
    LLE_TOKEN_CONTROL_CASE = 59,                      // case statements
    LLE_TOKEN_CONTROL_ESAC = 60,                      // esac keyword
    
    // File paths and arguments
    LLE_TOKEN_PATH_ABSOLUTE = 70,                     // Absolute file paths
    LLE_TOKEN_PATH_RELATIVE = 71,                     // Relative file paths
    LLE_TOKEN_PATH_HOME = 72,                         // Home directory paths (~)
    LLE_TOKEN_GLOB_PATTERN = 73,                      // Glob patterns (*, ?, [])
    LLE_TOKEN_FILE_DESCRIPTOR = 74,                   // File descriptor numbers
    LLE_TOKEN_COMMAND_OPTION = 75,                    // Command line options (-x, --option)
    
    // Comments and documentation
    LLE_TOKEN_COMMENT = 80,                           // Shell comments (#)
    LLE_TOKEN_SHEBANG = 81,                           // Shebang lines (#!/bin/bash)
    
    // Error conditions
    LLE_TOKEN_ERROR_SYNTAX = 90,                      // Syntax errors
    LLE_TOKEN_ERROR_UNMATCHED = 91,                   // Unmatched quotes or brackets
    LLE_TOKEN_ERROR_INVALID = 92,                     // Invalid token sequences
    
    // Maximum token type value
    LLE_TOKEN_MAX = 100
} lle_token_type_t;

// Syntax highlighting color scheme with theme integration
typedef struct lle_color_scheme {
    // Basic token colors
    lle_color_t unknown_color;                        // Unknown token color
    lle_color_t whitespace_color;                     // Whitespace color (usually transparent)
    lle_color_t word_color;                           // Default word color
    lle_color_t number_color;                         // Numeric literal color
    
    // Command and function colors
    lle_color_t command_color;                        // External command color
    lle_color_t builtin_color;                        // Builtin command color
    lle_color_t keyword_color;                        // Shell keyword color
    lle_color_t function_color;                       // Function name color
    lle_color_t alias_color;                          // Command alias color
    
    // Variable and expansion colors
    lle_color_t variable_color;                       // Variable reference color
    lle_color_t env_variable_color;                   // Environment variable color
    lle_color_t special_variable_color;               // Special variable color
    lle_color_t expansion_color;                      // Parameter/command expansion color
    
    // String and literal colors
    lle_color_t string_single_color;                  // Single-quoted string color
    lle_color_t string_double_color;                  // Double-quoted string color
    lle_color_t string_backtick_color;                // Backtick string color
    lle_color_t escape_color;                         // Escape sequence color
    lle_color_t here_doc_color;                       // Here document color
    
    // Operator and special character colors
    lle_color_t operator_color;                       // General operator color
    lle_color_t pipe_color;                           // Pipe operator color
    lle_color_t redirect_color;                       // Redirection operator color
    lle_color_t logical_color;                        // Logical operator color
    lle_color_t assignment_color;                     // Assignment operator color
    
    // Control flow colors
    lle_color_t control_color;                        // Control flow keyword color
    lle_color_t control_block_color;                  // Control block delimiter color
    
    // Path and argument colors
    lle_color_t path_color;                           // File path color
    lle_color_t glob_color;                           // Glob pattern color
    lle_color_t option_color;                         // Command option color
    
    // Comment and documentation colors
    lle_color_t comment_color;                        // Comment color
    lle_color_t shebang_color;                        // Shebang line color
    
    // Error condition colors
    lle_color_t error_color;                          // Syntax error color
    lle_color_t warning_color;                        // Warning condition color
    
    // Visual effects
    bool bold_keywords;                               // Bold formatting for keywords
    bool italic_comments;                             // Italic formatting for comments
    bool underline_errors;                            // Underline formatting for errors
    bool dim_comments;                                // Dimmed formatting for comments
} lle_color_scheme_t;
```

### 2.2 Real-Time Highlighting Pipeline

```c
// Real-time syntax highlighting processing pipeline
typedef struct lle_highlighting_pipeline {
    // Input processing
    lle_buffer_monitor_t *buffer_monitor;             // Buffer change monitoring
    lle_change_detector_t *change_detector;           // Incremental change detection
    lle_parsing_scheduler_t *parsing_scheduler;       // Intelligent parsing scheduling
    
    // Analysis stages
    lle_lexical_analyzer_t *lexical_analyzer;         // Lexical analysis and tokenization
    lle_syntax_validator_t *syntax_validator;         // Syntax validation and error detection
    lle_context_analyzer_t *context_analyzer;         // Context-aware token classification
    lle_semantic_analyzer_t *semantic_analyzer;       // Semantic analysis for advanced highlighting
    
    // Rendering pipeline
    lle_color_applicator_t *color_applicator;         // Color application and formatting
    lle_effect_processor_t *effect_processor;         // Visual effects processing
    lle_display_formatter_t *display_formatter;       // Display formatting and output
    
    // Performance optimization
    lle_cache_manager_t *cache_manager;               // Intelligent caching system
    lle_batch_processor_t *batch_processor;           // Batch processing for efficiency
    lle_priority_scheduler_t *priority_scheduler;     // Priority-based processing
    
    // State management
    lle_pipeline_state_t current_state;               // Current pipeline state
    lle_processing_metrics_t *metrics;                // Pipeline performance metrics
    
    // Thread coordination
    pthread_mutex_t pipeline_mutex;                   // Pipeline thread synchronization
    bool pipeline_active;                             // Pipeline operational status
} lle_highlighting_pipeline_t;

// Incremental parsing system for efficient real-time updates
typedef struct lle_incremental_parser {
    // Change tracking
    lle_buffer_diff_t *buffer_diff;                   // Buffer difference tracking
    lle_token_range_t *affected_range;                // Range of tokens affected by changes
    lle_invalidation_tracker_t *invalidation;         // Token invalidation tracking
    
    // Parsing state
    lle_parse_tree_t *current_tree;                   // Current syntax parse tree
    lle_parse_cache_t *parse_cache;                   // Cached parsing results
    lle_parsing_context_t *context_stack;             // Parsing context stack
    
    // Optimization strategies
    lle_reuse_analyzer_t *reuse_analyzer;             // Token reuse analysis
    lle_boundary_detector_t *boundary_detector;       // Parse boundary detection
    lle_fast_path_t *fast_path;                       // Fast path for simple changes
    
    // Performance monitoring
    lle_parse_metrics_t *parse_metrics;               // Parsing performance metrics
    size_t total_parses;                              // Total parsing operations
    size_t incremental_parses;                        // Successful incremental parses
    double average_parse_time;                        // Average parsing time in microseconds
} lle_incremental_parser_t;
```

---

## 3. Thread Safety Implementation

### 3.1 Thread Safety Architecture

The syntax highlighting system provides comprehensive thread safety through a multi-lock architecture designed for high-performance concurrent access while maintaining data consistency.

```c
/**
 * Initialize thread safety for syntax highlighting system
 */
lle_result_t lle_syntax_highlighting_system_init(lle_syntax_highlighting_system_t *system,
                                                lle_memory_pool_t *memory_pool) {
    if (!system || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Initialize all rwlocks for thread safety
    int result = pthread_rwlock_init(&system->widget_integration_lock, NULL);
    if (result != 0) {
        return LLE_ERROR_THREAD_SAFETY_INIT_FAILED;
    }
    
    result = pthread_rwlock_init(&system->terminal_integration_lock, NULL);
    if (result != 0) {
        pthread_rwlock_destroy(&system->widget_integration_lock);
        return LLE_ERROR_THREAD_SAFETY_INIT_FAILED;
    }
    
    result = pthread_rwlock_init(&system->highlighting_lock, NULL);
    if (result != 0) {
        pthread_rwlock_destroy(&system->widget_integration_lock);
        pthread_rwlock_destroy(&system->terminal_integration_lock);
        return LLE_ERROR_THREAD_SAFETY_INIT_FAILED;
    }
    
    // Initialize system state
    system->system_active = false;
    system->real_time_enabled = false;
    system->memory_pool = memory_pool;
    
    return LLE_SUCCESS;
}

/**
 * Cleanup thread safety resources
 */
lle_result_t lle_syntax_highlighting_system_destroy(lle_syntax_highlighting_system_t *system) {
    if (!system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Destroy all rwlocks
    int result1 = pthread_rwlock_destroy(&system->highlighting_lock);
    int result2 = pthread_rwlock_destroy(&system->terminal_integration_lock);
    int result3 = pthread_rwlock_destroy(&system->widget_integration_lock);
    
    if (result1 != 0 || result2 != 0 || result3 != 0) {
        return LLE_ERROR_THREAD_SAFETY_CLEANUP_FAILED;
    }
    
    return LLE_SUCCESS;
}
```

### 3.2 Thread-Safe Highlighting Operations

```c
/**
 * Thread-safe syntax highlighting with proper locking
 */
lle_result_t lle_syntax_highlight_buffer_threadsafe(lle_syntax_highlighting_system_t *system,
                                                   lle_buffer_t *buffer,
                                                   lle_highlight_result_t **result) {
    if (!system || !buffer || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Acquire highlighting read lock
    int lock_result = pthread_rwlock_rdlock(&system->highlighting_lock);
    if (lock_result != 0) {
        return LLE_ERROR_LOCK_FAILED;
    }
    
    lle_result_t highlight_result = LLE_SUCCESS;
    
    // Check system active status
    if (!system->system_active) {
        highlight_result = LLE_ERROR_SYSTEM_NOT_ACTIVE;
        goto cleanup;
    }
    
    // Perform highlighting operation
    highlight_result = lle_syntax_highlight_buffer_internal(system, buffer, result);
    
cleanup:
    pthread_rwlock_unlock(&system->highlighting_lock);
    return highlight_result;
}

/**
 * Thread-safe pipeline mutex operations
 */
lle_result_t lle_highlighting_pipeline_init(lle_highlighting_pipeline_t *pipeline) {
    if (!pipeline) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    int result = pthread_mutex_init(&pipeline->pipeline_mutex, NULL);
    if (result != 0) {
        return LLE_ERROR_THREAD_SAFETY_INIT_FAILED;
    }
    
    pipeline->pipeline_active = false;
    return LLE_SUCCESS;
}

lle_result_t lle_highlighting_pipeline_destroy(lle_highlighting_pipeline_t *pipeline) {
    if (!pipeline) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    int result = pthread_mutex_destroy(&pipeline->pipeline_mutex);
    if (result != 0) {
        return LLE_ERROR_THREAD_SAFETY_CLEANUP_FAILED;
    }
    
    return LLE_SUCCESS;
}
```

### 3.3 Lock Contention Monitoring

```c
/**
 * Thread safety performance monitoring
 */
typedef struct lle_syntax_thread_stats {
    uint64_t highlighting_lock_acquisitions;
    uint64_t widget_lock_acquisitions;
    uint64_t terminal_lock_acquisitions;
    uint64_t lock_contentions;
    uint64_t avg_lock_wait_time_us;
    float contention_ratio;
} lle_syntax_thread_stats_t;

lle_result_t lle_syntax_get_thread_stats(lle_syntax_highlighting_system_t *system,
                                        lle_syntax_thread_stats_t *stats) {
    if (!system || !stats) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Simple read of atomic counters - no locking needed for stats
    memset(stats, 0, sizeof(lle_syntax_thread_stats_t));
    
    // Note: In production, these would be atomic counters
    // This is a simplified implementation for documentation
    
    return LLE_SUCCESS;
}
```

### 3.4 Thread Safety Performance Characteristics

- **Read Lock Overhead**: < 30ns per highlighting operation
- **Write Lock Overhead**: < 80ns per system configuration change
- **Pipeline Mutex Overhead**: < 20ns per pipeline operation
- **Lock Contention Impact**: < 5% performance degradation under normal load
- **Memory Overhead**: 192 bytes per syntax highlighting system (3 rwlocks + 1 mutex)
- **Scalability**: Linear read performance scaling up to 16 concurrent highlighting operations

---

## 4. Widget Hook Integration

### 4.1 Widget Hook Integration Architecture

The syntax highlighting system integrates with the Advanced Prompt Widget Hooks system to provide syntax highlighting support for bottom-prompt mode, prompt state changes, and historical prompt modifications.

#### 4.1.1 Widget Integration System

```c
// NEW: Widget hook integration for syntax highlighting
typedef struct lle_widget_hook_integration {
    // Core integration
    lle_advanced_prompt_widget_hooks_t *widget_hooks; // Widget hook system reference
    lle_widget_highlight_engine_t *highlight_engine;  // Widget-specific highlighting
    lle_prompt_state_tracker_t *state_tracker;        // Track prompt state changes
    
    // Bottom-prompt support
    lle_bottom_prompt_highlighter_t *bottom_highlighter; // Bottom-prompt highlighting
    lle_historical_prompt_tracker_t *history_tracker;    // Historical prompt modifications
    lle_widget_context_manager_t *context_manager;       // Widget context management
    
    // Integration coordination
    lle_widget_callback_registry_t *callback_registry; // Widget callback management
    lle_highlight_state_sync_t *state_sync;           // State synchronization
    
    // Performance optimization
    lle_widget_highlight_cache_t *widget_cache;       // Widget highlighting cache
    memory_pool_t *widget_memory_pool;                // Widget-specific memory pool
} lle_widget_hook_integration_t;

// NEW: Widget-specific highlighting engine
typedef struct lle_widget_highlight_engine {
    // Bottom-prompt highlighting
    lle_bottom_prompt_parser_t *bottom_parser;        // Parse bottom-prompt syntax
    lle_historical_syntax_analyzer_t *history_analyzer; // Analyze historical commands
    lle_widget_token_classifier_t *widget_classifier; // Widget-aware token classification
    
    // Prompt state handling
    lle_prompt_transition_handler_t *transition_handler; // Handle prompt transitions
    lle_state_preservation_engine_t *state_preserving;   // Preserve highlighting state
    lle_context_restoration_t *context_restoration;      // Restore highlighting context
    
    // Performance metrics
    lle_widget_highlight_metrics_t *metrics;          // Widget highlighting performance
} lle_widget_highlight_engine_t;
```

#### 4.1.2 Widget Integration Implementation

```c
// NEW: Initialize widget hook integration
lle_result_t lle_syntax_init_widget_integration(
    lle_syntax_highlighting_system_t *system,
    lle_advanced_prompt_widget_hooks_t *widget_hooks
) {
    if (!system || !widget_hooks) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Allocate widget integration structure
    system->widget_integration = memory_pool_allocate(
        system->memory_pool,
        sizeof(lle_widget_hook_integration_t)
    );
    
    if (!system->widget_integration) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    system->widget_integration->widget_hooks = widget_hooks;
    
    // Initialize widget highlighting engine
    lle_result_t result = lle_init_widget_highlight_engine(
        &system->widget_integration->highlight_engine,
        system->memory_pool
    );
    
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Register widget callbacks
    lle_widget_hook_callbacks_t callbacks = {
        .on_prompt_mode_change = lle_syntax_on_prompt_mode_change,
        .on_bottom_prompt_activate = lle_syntax_on_bottom_prompt_activate,
        .on_bottom_prompt_deactivate = lle_syntax_on_bottom_prompt_deactivate,
        .on_historical_prompt_modify = lle_syntax_on_historical_prompt_modify,
        .context = system
    };
    
    return lle_widget_hooks_register_callbacks(
        widget_hooks,
        LLE_HOOK_CATEGORY_SYNTAX_HIGHLIGHTING,
        &callbacks
    );
}

// NEW: Handle prompt mode changes
lle_result_t lle_syntax_on_prompt_mode_change(
    lle_prompt_mode_t old_mode,
    lle_prompt_mode_t new_mode,
    void *context
) {
    lle_syntax_highlighting_system_t *system = (lle_syntax_highlighting_system_t*)context;
    
    // Update widget highlighting mode
    if (new_mode == LLE_PROMPT_BOTTOM_MODE) {
        system->widget_mode = LLE_WIDGET_HIGHLIGHT_BOTTOM;
        system->bottom_prompt_active = true;
        
        // Switch to bottom-prompt highlighting
        return lle_activate_bottom_prompt_highlighting(
            system->widget_integration->highlight_engine
        );
    } else {
        system->widget_mode = LLE_WIDGET_HIGHLIGHT_NORMAL;
        system->bottom_prompt_active = false;
        
        // Return to normal highlighting
        return lle_activate_normal_highlighting(
            system->widget_integration->highlight_engine
        );
    }
}

// NEW: Handle bottom-prompt activation
lle_result_t lle_syntax_on_bottom_prompt_activate(
    lle_bottom_prompt_context_t *context,
    void *user_context
) {
    lle_syntax_highlighting_system_t *system = (lle_syntax_highlighting_system_t*)user_context;
    
    // Configure highlighting for bottom-prompt context
    lle_result_t result = lle_configure_bottom_prompt_highlighting(
        system->widget_integration->highlight_engine->bottom_parser,
        context
    );
    
    if (result == LLE_SUCCESS) {
        system->last_widget_update = lle_get_microsecond_timestamp();
    }
    
    return result;
}

// NEW: Handle historical prompt modifications
lle_result_t lle_syntax_on_historical_prompt_modify(
    lle_history_entry_t *entry,
    const char *modified_text,
    void *context
) {
    lle_syntax_highlighting_system_t *system = (lle_syntax_highlighting_system_t*)context;
    
    // Switch to historical highlighting mode
    system->widget_mode = LLE_WIDGET_HIGHLIGHT_HISTORICAL;
    
    // Apply highlighting to historical command
    return lle_highlight_historical_command(
        system->widget_integration->highlight_engine->history_analyzer,
        entry,
        modified_text
    );
}
```

#### 4.1.3 Widget Integration Performance Requirements

- **Prompt Mode Switching**: <15us for switching between normal and bottom-prompt modes
- **Bottom-Prompt Highlighting**: <100us for full bottom-prompt syntax analysis
- **Historical Command Highlighting**: <50us for historical command syntax highlighting
- **Widget Callback Execution**: <25us for all widget callback processing

---

## 5. Adaptive Terminal Integration

### 5.1 Adaptive Terminal Integration Architecture

The syntax highlighting system integrates with the Adaptive Terminal Integration system to provide dynamic color capabilities with terminal-specific optimization and graceful fallbacks.

#### 5.1.1 Adaptive Terminal Integration System

```c
// NEW: Adaptive terminal integration for syntax highlighting
typedef struct lle_adaptive_terminal_integration {
    // Core integration
    lle_adaptive_terminal_integration_t *terminal_system; // Terminal integration reference
    lle_color_capability_detector_t *capability_detector; // Terminal color capabilities
    lle_adaptive_color_manager_t *adaptive_color_manager; // Adaptive color management
    
    // Color adaptation
    lle_terminal_color_mapper_t *color_mapper;         // Map colors to terminal capabilities
    lle_fallback_color_engine_t *fallback_engine;     // Graceful color fallbacks
    lle_color_optimization_t *color_optimizer;        // Terminal-specific optimizations
    
    // Capability management
    lle_color_profile_manager_t *profile_manager;     // Terminal color profiles
    lle_compatibility_matrix_t *compatibility_matrix; // Terminal compatibility data
    lle_dynamic_adaptation_t *dynamic_adapter;        // Runtime color adaptation
    
    // Performance optimization
    lle_terminal_color_cache_t *color_cache;          // Terminal-specific color cache
    memory_pool_t *terminal_memory_pool;              // Terminal integration memory pool
} lle_adaptive_terminal_integration_t;

// NEW: Color adaptation modes for different terminal types
typedef enum {
    LLE_COLOR_ADAPT_FULL,        // Full color support (24-bit)
    LLE_COLOR_ADAPT_256,         // 256-color support
    LLE_COLOR_ADAPT_16,          // 16-color support  
    LLE_COLOR_ADAPT_8,           // 8-color basic support
    LLE_COLOR_ADAPT_MONOCHROME,  // Monochrome fallback
    LLE_COLOR_ADAPT_NONE         // No color support
} lle_color_adaptation_mode_t;
```

#### 5.1.2 Adaptive Terminal Implementation

```c
// NEW: Initialize adaptive terminal integration
lle_result_t lle_syntax_init_adaptive_terminal(
    lle_syntax_highlighting_system_t *system,
    lle_adaptive_terminal_integration_t *terminal_integration
) {
    if (!system || !terminal_integration) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Allocate terminal integration structure
    system->terminal_integration = memory_pool_allocate(
        system->memory_pool,
        sizeof(lle_adaptive_terminal_integration_t)
    );
    
    if (!system->terminal_integration) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    system->terminal_integration->terminal_system = terminal_integration;
    
    // Initialize color capability detection
    lle_result_t result = lle_detect_terminal_color_capabilities(
        terminal_integration,
        &system->terminal_integration->capability_detector
    );
    
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Initialize adaptive color manager
    result = lle_init_adaptive_color_manager(
        &system->terminal_integration->adaptive_color_manager,
        system->terminal_integration->capability_detector,
        system->memory_pool
    );
    
    if (result == LLE_SUCCESS) {
        system->adaptive_colors_active = true;
    }
    
    return result;
}

// NEW: Adapt syntax colors to terminal capabilities
// Adapt syntax highlighting color scheme to current terminal capabilities
// 
// @param system: Syntax highlighting system instance
// @param color_scheme: Original color scheme to adapt
// @param adapted_scheme: Output buffer for terminal-adapted color scheme
//
// @returns:
//   - LLE_SUCCESS: Color adaptation completed successfully
//   - LLE_ERROR_INVALID_PARAMETER: NULL pointer or adaptive colors not active
//   - LLE_ERROR_TERMINAL_DETECTION_FAILED: Failed to detect terminal capabilities
//   - LLE_ERROR_COLOR_MAPPING_FAILED: Failed to map colors to terminal capabilities
//   - LLE_ERROR_MEMORY_ALLOCATION: Insufficient memory for color adaptation
//   - LLE_ERROR_TERMINAL_UNSUPPORTED: Terminal doesn't support required color features
lle_result_t lle_syntax_adapt_colors_to_terminal(
    lle_syntax_highlighting_system_t *system,
    lle_syntax_color_scheme_t *color_scheme,
    lle_adapted_color_scheme_t *adapted_scheme
) {
    if (!system || !color_scheme || !adapted_scheme || !system->adaptive_colors_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Detect current terminal color capabilities
    lle_color_capability_t capabilities;
    lle_result_t result = lle_get_current_color_capabilities(
        system->terminal_integration->capability_detector,
        &capabilities
    );
    
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Map colors based on terminal capabilities
    return lle_map_colors_to_terminal_capabilities(
        system->terminal_integration->color_mapper,
        color_scheme,
        &capabilities,
        adapted_scheme
    );
}

// NEW: Apply terminal-optimized syntax highlighting
lle_result_t lle_syntax_highlight_with_terminal_optimization(
    lle_syntax_highlighting_system_t *system,
    const char *text,
    size_t text_length,
    lle_highlight_result_t *result
) {
    if (!system || !text || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Perform standard syntax analysis
    lle_result_t analyze_result = lle_analyze_syntax_tokens(
        system->syntax_analyzer,
        text,
        text_length,
        result
    );
    
    if (analyze_result != LLE_SUCCESS) {
        return analyze_result;
    }
    
    // Apply terminal-specific color adaptation if available
    if (system->adaptive_colors_active) {
        return lle_apply_adaptive_colors(
            system->terminal_integration->adaptive_color_manager,
            result
        );
    }
    
    return LLE_SUCCESS;
}
```

#### 5.1.3 Terminal Integration Performance Requirements

- **Color Capability Detection**: <50us for terminal capability detection
- **Color Mapping**: <25us for mapping colors to terminal capabilities
- **Adaptive Color Application**: <30us for applying terminal-optimized colors
- **Terminal-Specific Optimization**: <100us total for complete terminal adaptation

---

## 6. Syntax Analysis Engine

### 6.1 Lexical Analysis Implementation

```c
// Complete lexical analysis system with comprehensive shell language support
lle_result_t lle_syntax_analyze_buffer(lle_syntax_analyzer_t *analyzer,
                                       const lle_buffer_t *buffer,
                                       lle_token_list_t **tokens) {
    // Validate input parameters
    if (!analyzer || !buffer || !tokens) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER, 
                                       "Invalid parameters for syntax analysis");
    }
    
    // Initialize tokenization state
    lle_tokenizer_state_t state = {
        .buffer = buffer,
        .position = 0,
        .line = 1,
        .column = 1,
        .context_stack = lle_context_stack_create(analyzer->memory_pool),
        .quote_state = LLE_QUOTE_NONE,
        .escape_active = false,
        .here_doc_delimiter = NULL
    };
    
    // Create token list with initial capacity
    *tokens = lle_token_list_create(analyzer->memory_pool, 256);
    if (!*tokens) {
        return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                       "Failed to allocate token list");
    }
    
    // Main tokenization loop with comprehensive error handling
    while (state.position < buffer->length) {
        lle_token_t *token = NULL;
        lle_result_t result;
        
        // Skip whitespace with position tracking
        if (lle_is_whitespace(buffer->data[state.position])) {
            result = lle_tokenize_whitespace(&state, &token);
            if (!lle_result_is_success(result)) {
                lle_token_list_destroy(*tokens);
                return result;
            }
        }
        // Handle comments with full line processing
        else if (buffer->data[state.position] == '#' && 
                 lle_is_comment_start(&state)) {
            result = lle_tokenize_comment(&state, &token);
            if (!lle_result_is_success(result)) {
                lle_token_list_destroy(*tokens);
                return result;
            }
        }
        // Process string literals with comprehensive quoting support
        else if (lle_is_quote_character(buffer->data[state.position])) {
            result = lle_tokenize_string_literal(&state, &token);
            if (!lle_result_is_success(result)) {
                lle_token_list_destroy(*tokens);
                return result;
            }
        }
        // Handle variable references and expansions
        else if (buffer->data[state.position] == '$') {
            result = lle_tokenize_variable_expansion(&state, &token);
            if (!lle_result_is_success(result)) {
                lle_token_list_destroy(*tokens);
                return result;
            }
        }
        // Process operators and special characters
        else if (lle_is_operator_character(buffer->data[state.position])) {
            result = lle_tokenize_operator(&state, &token);
            if (!lle_result_is_success(result)) {
                lle_token_list_destroy(*tokens);
                return result;
            }
        }
        // Handle numeric literals
        else if (lle_is_digit(buffer->data[state.position])) {
            result = lle_tokenize_number(&state, &token);
            if (!lle_result_is_success(result)) {
                lle_token_list_destroy(*tokens);
                return result;
            }
        }
        // Process word tokens (commands, keywords, identifiers)
        else if (lle_is_word_character(buffer->data[state.position])) {
            result = lle_tokenize_word(&state, &token);
            if (!lle_result_is_success(result)) {
                lle_token_list_destroy(*tokens);
                return result;
            }
        }
        // Handle unknown characters with error recovery
        else {
            result = lle_tokenize_unknown(&state, &token);
            if (!lle_result_is_success(result)) {
                lle_token_list_destroy(*tokens);
                return result;
            }
        }
        
        // Add token to list with validation
        if (token) {
            result = lle_token_list_append(*tokens, token);
            if (!lle_result_is_success(result)) {
                lle_token_destroy(token);
                lle_token_list_destroy(*tokens);
                return result;
            }
        }
    }
    
    // Post-processing: classify tokens and apply context-aware analysis
    lle_result_t classification_result = lle_classify_tokens(analyzer, *tokens);
    if (!lle_result_is_success(classification_result)) {
        lle_token_list_destroy(*tokens);
        return classification_result;
    }
    
    // Validate syntax and detect errors
    lle_result_t validation_result = lle_validate_syntax(analyzer, *tokens);
    if (!lle_result_is_success(validation_result)) {
        // Don't fail on syntax errors, just mark error tokens
        lle_log_warning("Syntax validation found errors in buffer");
    }
    
    return lle_result_create_success();
}

// Comprehensive word tokenization with command and keyword classification
lle_result_t lle_tokenize_word(lle_tokenizer_state_t *state, lle_token_t **token) {
    size_t start_position = state->position;
    size_t start_column = state->column;
    
    // Extract complete word including valid identifier characters
    while (state->position < state->buffer->length &&
           lle_is_word_character(state->buffer->data[state->position])) {
        state->position++;
        state->column++;
    }
    
    // Create token with word content
    size_t word_length = state->position - start_position;
    char *word_content = lle_memory_pool_allocate(state->analyzer->memory_pool,
                                                  word_length + 1);
    if (!word_content) {
        return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                       "Failed to allocate word token content");
    }
    
    memcpy(word_content, &state->buffer->data[start_position], word_length);
    word_content[word_length] = '\0';
    
    // Create token structure
    *token = lle_token_create(state->analyzer->memory_pool);
    if (!*token) {
        return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                       "Failed to allocate word token");
    }
    
    // Initialize token with position and content
    (*token)->type = LLE_TOKEN_WORD;  // Initial classification
    (*token)->content = word_content;
    (*token)->length = word_length;
    (*token)->line = state->line;
    (*token)->column = start_column;
    (*token)->position = start_position;
    
    return lle_result_create_success();
}

// Advanced string literal tokenization with comprehensive quoting support
lle_result_t lle_tokenize_string_literal(lle_tokenizer_state_t *state,
                                         lle_token_t **token) {
    char quote_char = state->buffer->data[state->position];
    size_t start_position = state->position;
    size_t start_column = state->column;
    bool escaped = false;
    
    // Move past opening quote
    state->position++;
    state->column++;
    
    // Process string content with escape handling
    while (state->position < state->buffer->length) {
        char current_char = state->buffer->data[state->position];
        
        // Handle escape sequences
        if (!escaped && current_char == '\\') {
            escaped = true;
            state->position++;
            state->column++;
            continue;
        }
        
        // Check for closing quote
        if (!escaped && current_char == quote_char) {
            state->position++;  // Include closing quote
            state->column++;
            break;
        }
        
        // Handle newlines in strings
        if (current_char == '\n') {
            state->line++;
            state->column = 1;
        } else {
            state->column++;
        }
        
        escaped = false;
        state->position++;
    }
    
    // Check for unterminated string
    if (state->position >= state->buffer->length &&
        state->buffer->data[state->position - 1] != quote_char) {
        // Create error token for unterminated string
        *token = lle_token_create(state->analyzer->memory_pool);
        if (!*token) {
            return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                           "Failed to allocate error token");
        }
        
        (*token)->type = LLE_TOKEN_ERROR_UNMATCHED;
        (*token)->position = start_position;
        (*token)->length = state->position - start_position;
        (*token)->line = state->line;
        (*token)->column = start_column;
        
        return lle_result_create_success();  // Continue parsing
    }
    
    // Create string literal token
    size_t token_length = state->position - start_position;
    char *string_content = lle_memory_pool_allocate(state->analyzer->memory_pool,
                                                    token_length + 1);
    if (!string_content) {
        return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                       "Failed to allocate string token content");
    }
    
    memcpy(string_content, &state->buffer->data[start_position], token_length);
    string_content[token_length] = '\0';
    
    *token = lle_token_create(state->analyzer->memory_pool);
    if (!*token) {
        return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                       "Failed to allocate string token");
    }
    
    // Classify string type based on quote character
    if (quote_char == '\'') {
        (*token)->type = LLE_TOKEN_STRING_SINGLE;
    } else if (quote_char == '"') {
        (*token)->type = LLE_TOKEN_STRING_DOUBLE;
    } else if (quote_char == '`') {
        (*token)->type = LLE_TOKEN_STRING_BACKTICK;
    }
    
    (*token)->content = string_content;
    (*token)->length = token_length;
    (*token)->line = state->line;
    (*token)->column = start_column;
    (*token)->position = start_position;
    
    return lle_result_create_success();
}
```

### 6.2 Token Classification System

```c
// Comprehensive token classification with context-aware analysis
lle_result_t lle_classify_tokens(lle_syntax_analyzer_t *analyzer,
                                 lle_token_list_t *tokens) {
    if (!analyzer || !tokens) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid parameters for token classification");
    }
    
    // Initialize classification context
    lle_classification_context_t context = {
        .command_position = true,   // First word is typically a command
        .in_assignment = false,
        .in_pipe = false,
        .in_redirection = false,
        .brace_depth = 0,
        .paren_depth = 0,
        .current_function = NULL
    };
    
    // Process each token with context-aware classification
    for (size_t i = 0; i < tokens->count; i++) {
        lle_token_t *token = tokens->tokens[i];
        lle_token_t *prev_token = (i > 0) ? tokens->tokens[i - 1] : NULL;
        lle_token_t *next_token = (i < tokens->count - 1) ? tokens->tokens[i + 1] : NULL;
        
        // Skip whitespace tokens for context analysis
        if (token->type == LLE_TOKEN_WHITESPACE) {
            continue;
        }
        
        // Classify based on token type and context
        lle_result_t result;
        switch (token->type) {
            case LLE_TOKEN_WORD:
                result = lle_classify_word_token(analyzer, token, &context,
                                                 prev_token, next_token);
                break;
                
            case LLE_TOKEN_OPERATOR:
                result = lle_classify_operator_token(analyzer, token, &context);
                break;
                
            case LLE_TOKEN_NUMBER:
                result = lle_classify_number_token(analyzer, token, &context);
                break;
                
            default:
                result = lle_result_create_success();  // Already classified
                break;
        }
        
        if (!lle_result_is_success(result)) {
            return result;
        }
        
        // Update context based on classified token
        lle_update_classification_context(&context, token);
    }
    
    return lle_result_create_success();
}

// Word token classification with comprehensive shell language support
lle_result_t lle_classify_word_token(lle_syntax_analyzer_t *analyzer,
                                     lle_token_t *token,
                                     lle_classification_context_t *context,
                                     lle_token_t *prev_token,
                                     lle_token_t *next_token) {
    // Check for shell keywords first
    if (lle_is_shell_keyword(token->content)) {
        token->type = lle_get_keyword_token_type(token->content);
        context->command_position = lle_keyword_starts_command(token->content);
        return lle_result_create_success();
    }
    
    // Check for builtin commands
    if (lle_is_builtin_command(analyzer->keyword_db, token->content)) {
        token->type = LLE_TOKEN_BUILTIN;
        context->command_position = false;
        return lle_result_create_success();
    }
    
    // Check for function names
    if (next_token && strcmp(next_token->content, "()") == 0) {
        token->type = LLE_TOKEN_FUNCTION;
        context->current_function = token->content;
        return lle_result_create_success();
    }
    
    // Check for command aliases
    if (context->command_position &&
        lle_is_command_alias(analyzer->alias_db, token->content)) {
        token->type = LLE_TOKEN_ALIAS;
        context->command_position = false;
        return lle_result_create_success();
    }
    
    // Check for environment variables
    if (lle_is_environment_variable(token->content)) {
        token->type = LLE_TOKEN_ENV_VARIABLE;
        return lle_result_create_success();
    }
    
    // Check for file paths
    if (lle_looks_like_path(token->content)) {
        if (token->content[0] == '/') {
            token->type = LLE_TOKEN_PATH_ABSOLUTE;
        } else if (token->content[0] == '~') {
            token->type = LLE_TOKEN_PATH_HOME;
        } else {
            token->type = LLE_TOKEN_PATH_RELATIVE;
        }
        return lle_result_create_success();
    }
    
    // Check for glob patterns
    if (lle_contains_glob_characters(token->content)) {
        token->type = LLE_TOKEN_GLOB_PATTERN;
        return lle_result_create_success();
    }
    
    // Check for command options
    if (token->content[0] == '-' && strlen(token->content) > 1) {
        token->type = LLE_TOKEN_COMMAND_OPTION;
        return lle_result_create_success();
    }
    
    // Default classification based on context
    if (context->command_position) {
        token->type = LLE_TOKEN_COMMAND;
        context->command_position = false;
    } else {
        token->type = LLE_TOKEN_WORD;  // Keep as generic word
    }
    
    return lle_result_create_success();
}

// Shell keyword detection with comprehensive language support
bool lle_is_shell_keyword(const char *word) {
    static const char *shell_keywords[] = {
        // Control flow keywords
        "if", "then", "else", "elif", "fi",
        "case", "esac", "in",
        "for", "while", "until", "do", "done",
        "break", "continue", "return", "exit",
        
        // Function and scope keywords
        "function", "local", "readonly", "export",
        "declare", "typeset", "unset",
        
        // Conditional keywords
        "test", "[", "[[",
        
        // Time and execution keywords
        "time", "exec", "eval",
        
        // Job control keywords
        "jobs", "bg", "fg", "disown", "nohup",
        
        NULL
    };
    
    for (int i = 0; shell_keywords[i] != NULL; i++) {
        if (strcmp(word, shell_keywords[i]) == 0) {
            return true;
        }
    }
    
    return false;
}

// Get specific keyword token type for precise highlighting
lle_token_type_t lle_get_keyword_token_type(const char *keyword) {
    if (strcmp(keyword, "if") == 0) return LLE_TOKEN_CONTROL_IF;
    if (strcmp(keyword, "then") == 0) return LLE_TOKEN_CONTROL_THEN;
    if (strcmp(keyword, "else") == 0) return LLE_TOKEN_CONTROL_ELSE;
    if (strcmp(keyword, "elif") == 0) return LLE_TOKEN_CONTROL_ELIF;
    if (strcmp(keyword, "fi") == 0) return LLE_TOKEN_CONTROL_FI;
    if (strcmp(keyword, "for") == 0) return LLE_TOKEN_CONTROL_FOR;
    if (strcmp(keyword, "while") == 0) return LLE_TOKEN_CONTROL_WHILE;
    if (strcmp(keyword, "do") == 0) return LLE_TOKEN_CONTROL_DO;
    if (strcmp(keyword, "done") == 0) return LLE_TOKEN_CONTROL_DONE;
    if (strcmp(keyword, "case") == 0) return LLE_TOKEN_CONTROL_CASE;
    if (strcmp(keyword, "esac") == 0) return LLE_TOKEN_CONTROL_ESAC;
    
    // Default to generic keyword
    return LLE_TOKEN_KEYWORD;
}
```

### 3.3 Syntax Validation and Error Detection

```c
// Comprehensive syntax validation with detailed error reporting
lle_result_t lle_validate_syntax(lle_syntax_analyzer_t *analyzer,
                                 lle_token_list_t *tokens) {
    if (!analyzer || !tokens) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid parameters for syntax validation");
    }
    
    // Initialize validation state
    lle_validation_state_t state = {
        .quote_stack = lle_stack_create(analyzer->memory_pool),
        .brace_stack = lle_stack_create(analyzer->memory_pool),
        .control_stack = lle_stack_create(analyzer->memory_pool),
        .error_count = 0,
        .warning_count = 0
    };
    
    // Validate token sequence for syntax errors
    for (size_t i = 0; i < tokens->count; i++) {
        lle_token_t *token = tokens->tokens[i];
        
        switch (token->type) {
            case LLE_TOKEN_STRING_SINGLE:
            case LLE_TOKEN_STRING_DOUBLE:
            case LLE_TOKEN_STRING_BACKTICK:
                lle_validate_string_token(&state, token);
                break;
                
            case LLE_TOKEN_CONTROL_IF:
                lle_stack_push(state.control_stack, "if");
                break;
                
            case LLE_TOKEN_CONTROL_FI:
                if (lle_stack_is_empty(state.control_stack) ||
                    strcmp(lle_stack_top(state.control_stack), "if") != 0) {
                    token->type = LLE_TOKEN_ERROR_UNMATCHED;
                    state.error_count++;
                } else {
                    lle_stack_pop(state.control_stack);
                }
                break;
                
            case LLE_TOKEN_CONTROL_FOR:
            case LLE_TOKEN_CONTROL_WHILE:
                lle_stack_push(state.control_stack, 
                              (token->type == LLE_TOKEN_CONTROL_FOR) ? "for" : "while");
                break;
                
            case LLE_TOKEN_CONTROL_DONE:
                if (lle_stack_is_empty(state.control_stack)) {
                    token->type = LLE_TOKEN_ERROR_UNMATCHED;
                    state.error_count++;
                } else {
                    const char *top = lle_stack_top(state.control_stack);
                    if (strcmp(top, "for") == 0 || strcmp(top, "while") == 0) {
                        lle_stack_pop(state.control_stack);
                    } else {
                        token->type = LLE_TOKEN_ERROR_UNMATCHED;
                        state.error_count++;
                    }
                }
                break;
                
            case LLE_TOKEN_PIPE:
                lle_validate_pipe_sequence(&state, tokens, i);
                break;
                
            case LLE_TOKEN_REDIRECT:
                lle_validate_redirection(&state, tokens, i);
                break;
                
            default:
                break;
        }
    }
    
    // Check for unclosed control structures
    while (!lle_stack_is_empty(state.control_stack)) {
        state.error_count++;
        lle_stack_pop(state.control_stack);
    }
    
    // Clean up validation state
    lle_stack_destroy(state.quote_stack);
    lle_stack_destroy(state.brace_stack);
    lle_stack_destroy(state.control_stack);
    
    if (state.error_count > 0) {
        return lle_result_create_warning(LLE_WARNING_SYNTAX_ERRORS,
                                         "Syntax validation found errors");
    }
    
    return lle_result_create_success();
}
```

---

## 4. Real-Time Highlighting System

### 4.1 Real-Time Update Processing

```c
// Real-time highlighting update system with intelligent change detection
lle_result_t lle_highlighting_update_realtime(lle_syntax_highlighting_system_t *system,
                                              const lle_buffer_change_t *change) {
    if (!system || !change || !system->system_active) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid parameters for real-time update");
    }
    
    // Record performance start time
    uint64_t start_time = lle_get_microseconds();
    
    // Determine change impact scope
    lle_change_scope_t scope = lle_analyze_change_scope(system, change);
    
    // Select optimization strategy based on change type
    lle_result_t result;
    switch (scope.type) {
        case LLE_CHANGE_MINIMAL:
            // Single character insertion/deletion - use fast path
            result = lle_highlighting_update_minimal(system, change, &scope);
            break;
            
        case LLE_CHANGE_LOCAL:
            // Local word/token changes - incremental parsing
            result = lle_highlighting_update_incremental(system, change, &scope);
            break;
            
        case LLE_CHANGE_STRUCTURAL:
            // Structural changes affecting syntax - full reparse of affected region
            result = lle_highlighting_update_structural(system, change, &scope);
            break;
            
        case LLE_CHANGE_GLOBAL:
            // Global changes requiring full buffer reanalysis
            result = lle_highlighting_update_full(system, change, &scope);
            break;
            
        default:
            return lle_result_create_error(LLE_ERROR_INVALID_STATE,
                                           "Unknown change scope type");
    }
    
    // Record performance metrics
    uint64_t end_time = lle_get_microseconds();
    double update_time = (end_time - start_time) / 1000.0;
    
    lle_syntax_metrics_record_update(system->perf_metrics, update_time, scope.type);
    
    // Verify performance target
    if (update_time > LLE_SYNTAX_MAX_UPDATE_TIME_MS) {
        lle_log_warning("Syntax highlighting update exceeded performance target: %.2f ms",
                        update_time);
    }
    
    return result;
}

// Minimal change fast path for single character operations
lle_result_t lle_highlighting_update_minimal(lle_syntax_highlighting_system_t *system,
                                             const lle_buffer_change_t *change,
                                             const lle_change_scope_t *scope) {
    // Fast path for single character insertion/deletion within existing token
    if (scope->affected_tokens == 1 && 
        change->length <= 1 &&
        !lle_change_affects_syntax(change)) {
        
        // Simply extend or shrink the existing token
        lle_token_t *affected_token = scope->first_affected_token;
        
        if (change->type == LLE_BUFFER_CHANGE_INSERT) {
            affected_token->length += change->length;
            
            // Check if character insertion changes token classification
            if (lle_character_affects_classification(change->text[0], affected_token)) {
                // Reclassify token with new content
                return lle_reclassify_single_token(system, affected_token);
            }
        } else if (change->type == LLE_BUFFER_CHANGE_DELETE) {
            affected_token->length -= change->length;
            
            // Validate token still has content
            if (affected_token->length == 0) {
                return lle_remove_empty_token(system, affected_token);
            }
        }
        
        // Update display for affected token only
        return lle_display_update_token(system->display_coordinator, affected_token);
    }
    
    // Fall back to incremental update for more complex minimal changes
    return lle_highlighting_update_incremental(system, change, scope);
}

// Incremental parsing for local changes
lle_result_t lle_highlighting_update_incremental(lle_syntax_highlighting_system_t *system,
                                                 const lle_buffer_change_t *change,
                                                 const lle_change_scope_t *scope) {
    // Determine parsing boundaries for incremental update
    lle_parse_boundaries_t boundaries = lle_calculate_parse_boundaries(system, change, scope);
    
    // Extract affected buffer region
    lle_buffer_region_t region = {
        .start = boundaries.parse_start,
        .end = boundaries.parse_end,
        .buffer = system->buffer_integration->current_buffer
    };
    
    // Perform incremental parsing on affected region
    lle_token_list_t *new_tokens = NULL;
    lle_result_t parse_result = lle_syntax_analyze_region(system->syntax_analyzer,
                                                          &region, &new_tokens);
    if (!lle_result_is_success(parse_result)) {
        return parse_result;
    }
    
    // Replace affected tokens with newly parsed tokens
    lle_result_t replace_result = lle_token_list_replace_range(
        system->current_tokens,
        boundaries.token_start_index,
        boundaries.token_end_index,
        new_tokens
    );
    
    if (!lle_result_is_success(replace_result)) {
        lle_token_list_destroy(new_tokens);
        return replace_result;
    }
    
    // Update display for affected region
    lle_result_t display_result = lle_display_update_token_range(
        system->display_coordinator,
        boundaries.token_start_index,
        boundaries.token_start_index + new_tokens->count
    );
    
    lle_token_list_destroy(new_tokens);
    return display_result;
}
```

### 4.2 Performance Optimization System

```c
// Intelligent caching system for syntax highlighting performance
typedef struct lle_syntax_cache {
    // Token cache for rapid lookup
    lle_hash_table_t *token_cache;                     // Pre-classified token cache
    lle_hash_table_t *keyword_cache;                   // Keyword classification cache
    lle_hash_table_t *pattern_cache;                   // Pattern matching results cache
    
    // Parse tree cache for incremental parsing
    lle_hash_table_t *parse_tree_cache;                // Cached parse trees by content hash
    lle_lru_cache_t *recent_parses;                    // LRU cache for recent parsing results
    
    // Color application cache
    lle_hash_table_t *color_cache;                     // Pre-computed color applications
    lle_theme_cache_t *theme_cache;                    // Cached theme color mappings
    
    // Performance metrics
    size_t cache_hits;                                 // Total cache hit count
    size_t cache_misses;                               // Total cache miss count
    double cache_hit_rate;                             // Current cache hit rate
    
    // Cache management
    size_t max_cache_size;                             // Maximum cache size
    lle_cache_eviction_policy_t eviction_policy;       // Cache eviction strategy
    
    // Memory pool integration
    memory_pool_t *cache_memory_pool;                  // Dedicated cache memory pool
} lle_syntax_cache_t;

// Cache-optimized token lookup with intelligent prefetching
lle_result_t lle_syntax_cache_lookup_token(lle_syntax_cache_t *cache,
                                           const char *token_content,
                                           size_t content_length,
                                           lle_token_classification_t **result) {
    if (!cache || !token_content || !result) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid parameters for cache lookup");
    }
    
    // Generate cache key from token content
    uint64_t cache_key = lle_hash_fnv1a(token_content, content_length);
    
    // Attempt cache lookup
    lle_token_classification_t *cached_result = NULL;
    if (lle_hash_table_get(cache->token_cache, cache_key, (void**)&cached_result)) {
        cache->cache_hits++;
        *result = cached_result;
        
        // Update LRU cache position
        lle_lru_cache_touch(cache->recent_parses, cache_key);
        
        return lle_result_create_success();
    }
    
    // Cache miss - need to compute classification
    cache->cache_misses++;
    *result = NULL;
    
    // Update cache hit rate
    cache->cache_hit_rate = (double)cache->cache_hits / 
                           (cache->cache_hits + cache->cache_misses);
    
    return lle_result_create_miss();
}

// Cache storage with intelligent eviction
lle_result_t lle_syntax_cache_store_token(lle_syntax_cache_t *cache,
                                          const char *token_content,
                                          size_t content_length,
                                          const lle_token_classification_t *classification) {
    if (!cache || !token_content || !classification) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid parameters for cache storage");
    }
    
    // Check cache size limits
    if (lle_hash_table_size(cache->token_cache) >= cache->max_cache_size) {
        // Perform cache eviction based on policy
        lle_result_t eviction_result = lle_syntax_cache_evict(cache);
        if (!lle_result_is_success(eviction_result)) {
            return eviction_result;
        }
    }
    
    // Generate cache key
    uint64_t cache_key = lle_hash_fnv1a(token_content, content_length);
    
    // Clone classification for storage
    lle_token_classification_t *stored_classification = 
        lle_token_classification_clone(cache->cache_memory_pool, classification);
    if (!stored_classification) {
        return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                       "Failed to allocate cached classification");
    }
    
    // Store in cache
    lle_result_t store_result = lle_hash_table_set(cache->token_cache,
                                                   cache_key,
                                                   stored_classification);
    if (!lle_result_is_success(store_result)) {
        lle_token_classification_destroy(stored_classification);
        return store_result;
    }
    
    // Update LRU cache
    lle_lru_cache_insert(cache->recent_parses, cache_key, stored_classification);
    
    return lle_result_create_success();
}
```

---

## 5. Color Management

### 5.1 Dynamic Color System

```c
// Dynamic color management with theme integration
typedef struct lle_color_manager {
    // Active color scheme
    lle_color_scheme_t *active_scheme;                 // Currently active color scheme
    lle_color_palette_t *base_palette;                 // Base color palette
    lle_color_adaptation_t *adaptation_engine;         // Color adaptation engine
    
    // Theme integration
    lle_theme_integration_t *theme_integration;        // Lush theme system integration
    lle_color_mapping_t *theme_mappings;               // Theme-specific color mappings
    lle_dynamic_colors_t *dynamic_colors;              // Dynamic color adjustment
    
    // Terminal capability
    lle_terminal_colors_t *terminal_colors;            // Terminal color capabilities
    lle_color_fallback_t *fallback_system;             // Color fallback system
    
    // Performance optimization
    lle_color_cache_t *color_cache;                    // Pre-computed color combinations
    lle_hash_table_t *ansi_cache;                      // ANSI escape sequence cache
    
    // Configuration
    lle_color_config_t *config;                        // Color system configuration
    bool colors_enabled;                               // Color highlighting enabled
    bool true_color_supported;                         // Terminal true color support
} lle_color_manager_t;

// Comprehensive color application with terminal optimization
lle_result_t lle_color_apply_to_token(lle_color_manager_t *manager,
                                      lle_token_t *token,
                                      lle_color_application_t **result) {
    if (!manager || !token || !result || !manager->colors_enabled) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid parameters for color application");
    }
    
    // Get base color for token type
    lle_color_t base_color;
    lle_result_t color_result = lle_get_token_color(manager->active_scheme,
                                                    token->type, &base_color);
    if (!lle_result_is_success(color_result)) {
        return color_result;
    }
    
    // Apply contextual color modifications
    lle_color_t final_color = base_color;
    
    // Check for error conditions requiring color override
    if (token->type >= LLE_TOKEN_ERROR_SYNTAX) {
        final_color = manager->active_scheme->error_color;
    }
    
    // Apply visual effects based on token properties
    lle_visual_effects_t effects = { .bold = false, .italic = false, 
                                     .underline = false, .dim = false };
    
    switch (token->type) {
        case LLE_TOKEN_KEYWORD:
        case LLE_TOKEN_CONTROL_IF:
        case LLE_TOKEN_CONTROL_FOR:
        case LLE_TOKEN_CONTROL_WHILE:
            if (manager->active_scheme->bold_keywords) {
                effects.bold = true;
            }
            break;
            
        case LLE_TOKEN_COMMENT:
            if (manager->active_scheme->italic_comments) {
                effects.italic = true;
            }
            if (manager->active_scheme->dim_comments) {
                effects.dim = true;
            }
            break;
            
        case LLE_TOKEN_ERROR_SYNTAX:
        case LLE_TOKEN_ERROR_UNMATCHED:
            if (manager->active_scheme->underline_errors) {
                effects.underline = true;
            }
            break;
            
        default:
            break;
    }
    
    // Generate terminal-specific color sequence
    lle_color_sequence_t *color_sequence = NULL;
    lle_result_t sequence_result = lle_generate_color_sequence(
        manager->terminal_colors,
        &final_color,
        &effects,
        &color_sequence
    );
    
    if (!lle_result_is_success(sequence_result)) {
        return sequence_result;
    }
    
    // Create color application result
    *result = lle_color_application_create(manager->color_cache->cache_memory_pool);
    if (!*result) {
        lle_color_sequence_destroy(color_sequence);
        return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                       "Failed to allocate color application");
    }
    
    (*result)->token = token;
    (*result)->base_color = base_color;
    (*result)->final_color = final_color;
    (*result)->effects = effects;
    (*result)->color_sequence = color_sequence;
    (*result)->sequence_length = color_sequence->length;
    
    return lle_result_create_success();
}

// Terminal color capability detection and adaptation
lle_result_t lle_detect_terminal_color_support(lle_terminal_colors_t *terminal_colors) {
    // Initialize color capability structure
    terminal_colors->color_count = 0;
    terminal_colors->true_color_supported = false;
    terminal_colors->color_palette_supported = false;
    terminal_colors->basic_colors_only = true;
    
    // Check COLORTERM environment variable for true color support
    const char *colorterm = getenv("COLORTERM");
    if (colorterm && (strstr(colorterm, "truecolor") || strstr(colorterm, "24bit"))) {
        terminal_colors->true_color_supported = true;
        terminal_colors->color_count = 16777216;  // 24-bit color
        terminal_colors->basic_colors_only = false;
        return lle_result_create_success();
    }
    
    // Check TERM environment variable for color capabilities
    const char *term = getenv("TERM");
    if (term) {
        if (strstr(term, "256color")) {
            terminal_colors->color_palette_supported = true;
            terminal_colors->color_count = 256;
            terminal_colors->basic_colors_only = false;
        } else if (strstr(term, "color")) {
            terminal_colors->color_count = 16;  // Basic 16-color support
            terminal_colors->basic_colors_only = false;
        } else {
            terminal_colors->color_count = 0;   // No color support
            terminal_colors->basic_colors_only = true;
        }
    }
    
    // Perform runtime capability testing if needed
    if (terminal_colors->color_count == 0) {
        lle_result_t test_result = lle_test_terminal_color_support(terminal_colors);
        if (!lle_result_is_success(test_result)) {
            // Default to no color support on test failure
            terminal_colors->basic_colors_only = true;
            terminal_colors->color_count = 0;
        }
    }
    
    return lle_result_create_success();
}
```

---

## 6. Shell Language Support

### 6.1 Comprehensive Shell Grammar Implementation

```c
// Complete shell language grammar with comprehensive pattern matching
typedef struct lle_shell_grammar {
    // Grammar rules and patterns
    lle_grammar_rules_t *syntax_rules;                // Complete syntax rules
    lle_pattern_database_t *pattern_db;               // Pattern matching database
    lle_regex_engine_t *regex_engine;                 // Regular expression engine
    
    // Language-specific components
    lle_bash_grammar_t *bash_grammar;                 // Bash-specific grammar rules
    lle_zsh_grammar_t *zsh_grammar;                   // Zsh-specific grammar rules
    lle_posix_grammar_t *posix_grammar;               // POSIX shell grammar rules
    
    // Context-aware parsing
    lle_context_tracker_t *context_tracker;           // Parsing context tracker
    lle_scope_analyzer_t *scope_analyzer;             // Variable and function scope analysis
    
    // Memory management
    memory_pool_t *grammar_memory_pool;               // Grammar-specific memory pool
} lle_shell_grammar_t;

// Shell operator recognition with comprehensive coverage
static const lle_operator_definition_t shell_operators[] = {
    // Pipe operators
    { "|",    LLE_TOKEN_PIPE,         LLE_PRECEDENCE_PIPE,      LLE_ASSOC_LEFT },
    { "||",   LLE_TOKEN_LOGICAL,      LLE_PRECEDENCE_LOGICAL,   LLE_ASSOC_LEFT },
    
    // Redirection operators
    { "<",    LLE_TOKEN_REDIRECT,     LLE_PRECEDENCE_REDIRECT,  LLE_ASSOC_NONE },
    { ">",    LLE_TOKEN_REDIRECT,     LLE_PRECEDENCE_REDIRECT,  LLE_ASSOC_NONE },
    { ">>",   LLE_TOKEN_REDIRECT,     LLE_PRECEDENCE_REDIRECT,  LLE_ASSOC_NONE },
    { "<<<",  LLE_TOKEN_REDIRECT,     LLE_PRECEDENCE_REDIRECT,  LLE_ASSOC_NONE },
    { "<&",   LLE_TOKEN_REDIRECT,     LLE_PRECEDENCE_REDIRECT,  LLE_ASSOC_NONE },
    { ">&",   LLE_TOKEN_REDIRECT,     LLE_PRECEDENCE_REDIRECT,  LLE_ASSOC_NONE },
    { "<>",   LLE_TOKEN_REDIRECT,     LLE_PRECEDENCE_REDIRECT,  LLE_ASSOC_NONE },
    
    // Logical operators
    { "&&",   LLE_TOKEN_LOGICAL,      LLE_PRECEDENCE_LOGICAL,   LLE_ASSOC_LEFT },
    { "!",    LLE_TOKEN_LOGICAL,      LLE_PRECEDENCE_UNARY,     LLE_ASSOC_RIGHT },
    
    // Assignment operators
    { "=",    LLE_TOKEN_ASSIGNMENT,   LLE_PRECEDENCE_ASSIGN,    LLE_ASSOC_RIGHT },
    { "+=",   LLE_TOKEN_ASSIGNMENT,   LLE_PRECEDENCE_ASSIGN,    LLE_ASSOC_RIGHT },
    { "-=",   LLE_TOKEN_ASSIGNMENT,   LLE_PRECEDENCE_ASSIGN,    LLE_ASSOC_RIGHT },
    { "*=",   LLE_TOKEN_ASSIGNMENT,   LLE_PRECEDENCE_ASSIGN,    LLE_ASSOC_RIGHT },
    { "/=",   LLE_TOKEN_ASSIGNMENT,   LLE_PRECEDENCE_ASSIGN,    LLE_ASSOC_RIGHT },
    { "%=",   LLE_TOKEN_ASSIGNMENT,   LLE_PRECEDENCE_ASSIGN,    LLE_ASSOC_RIGHT },
    
    // Comparison operators
    { "==",   LLE_TOKEN_COMPARISON,   LLE_PRECEDENCE_COMPARE,   LLE_ASSOC_LEFT },
    { "!=",   LLE_TOKEN_COMPARISON,   LLE_PRECEDENCE_COMPARE,   LLE_ASSOC_LEFT },
    { "<=",   LLE_TOKEN_COMPARISON,   LLE_PRECEDENCE_COMPARE,   LLE_ASSOC_LEFT },
    { ">=",   LLE_TOKEN_COMPARISON,   LLE_PRECEDENCE_COMPARE,   LLE_ASSOC_LEFT },
    { "-eq",  LLE_TOKEN_COMPARISON,   LLE_PRECEDENCE_COMPARE,   LLE_ASSOC_LEFT },
    { "-ne",  LLE_TOKEN_COMPARISON,   LLE_PRECEDENCE_COMPARE,   LLE_ASSOC_LEFT },
    { "-lt",  LLE_TOKEN_COMPARISON,   LLE_PRECEDENCE_COMPARE,   LLE_ASSOC_LEFT },
    { "-le",  LLE_TOKEN_COMPARISON,   LLE_PRECEDENCE_COMPARE,   LLE_ASSOC_LEFT },
    { "-gt",  LLE_TOKEN_COMPARISON,   LLE_PRECEDENCE_COMPARE,   LLE_ASSOC_LEFT },
    { "-ge",  LLE_TOKEN_COMPARISON,   LLE_PRECEDENCE_COMPARE,   LLE_ASSOC_LEFT },
    
    // Arithmetic operators
    { "+",    LLE_TOKEN_ARITHMETIC,   LLE_PRECEDENCE_ADD,       LLE_ASSOC_LEFT },
    { "-",    LLE_TOKEN_ARITHMETIC,   LLE_PRECEDENCE_ADD,       LLE_ASSOC_LEFT },
    { "*",    LLE_TOKEN_ARITHMETIC,   LLE_PRECEDENCE_MULT,      LLE_ASSOC_LEFT },
    { "/",    LLE_TOKEN_ARITHMETIC,   LLE_PRECEDENCE_MULT,      LLE_ASSOC_LEFT },
    { "%",    LLE_TOKEN_ARITHMETIC,   LLE_PRECEDENCE_MULT,      LLE_ASSOC_LEFT },
    { "**",   LLE_TOKEN_ARITHMETIC,   LLE_PRECEDENCE_POWER,     LLE_ASSOC_RIGHT },
    
    // String operators
    { "=~",   LLE_TOKEN_COMPARISON,   LLE_PRECEDENCE_COMPARE,   LLE_ASSOC_LEFT },
    
    // Background and job control
    { "&",    LLE_TOKEN_OPERATOR,     LLE_PRECEDENCE_BACKGROUND, LLE_ASSOC_NONE },
    { ";&",   LLE_TOKEN_OPERATOR,     LLE_PRECEDENCE_BACKGROUND, LLE_ASSOC_NONE },
    { ";",    LLE_TOKEN_OPERATOR,     LLE_PRECEDENCE_SEQUENCE,   LLE_ASSOC_NONE },
    
    // Terminator
    { NULL,   LLE_TOKEN_UNKNOWN,      0,                         LLE_ASSOC_NONE }
};

// Variable expansion pattern recognition
lle_result_t lle_analyze_variable_expansion(lle_shell_grammar_t *grammar,
                                           const char *text, size_t length,
                                           lle_expansion_analysis_t *analysis) {
    if (!grammar || !text || length == 0 || !analysis) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid parameters for variable expansion analysis");
    }
    
    // Initialize analysis structure
    memset(analysis, 0, sizeof(lle_expansion_analysis_t));
    analysis->expansion_type = LLE_EXPANSION_NONE;
    analysis->start_pos = 0;
    analysis->length = 0;
    
    // Must start with $ for any expansion
    if (text[0] != '$') {
        return lle_result_create_success();  // No expansion found
    }
    
    // Single character variables: $?, $#, $@, $$, etc.
    if (length == 2 && lle_is_special_variable_char(text[1])) {
        analysis->expansion_type = LLE_EXPANSION_SPECIAL_VARIABLE;
        analysis->start_pos = 0;
        analysis->length = 2;
        analysis->variable_name = lle_string_clone_range(grammar->grammar_memory_pool, 
                                                         text, 1, 1);
        return lle_result_create_success();
    }
    
    // Simple variable reference: $VAR
    if (length > 1 && lle_is_identifier_start(text[1])) {
        size_t var_end = 1;
        while (var_end < length && lle_is_identifier_char(text[var_end])) {
            var_end++;
        }
        
        analysis->expansion_type = LLE_EXPANSION_SIMPLE_VARIABLE;
        analysis->start_pos = 0;
        analysis->length = var_end;
        analysis->variable_name = lle_string_clone_range(grammar->grammar_memory_pool,
                                                         text, 1, var_end - 1);
        return lle_result_create_success();
    }
    
    // Complex parameter expansion: ${VAR...}
    if (length > 2 && text[1] == '{') {
        size_t brace_end = lle_find_matching_brace(text, 1, length);
        if (brace_end == SIZE_MAX) {
            // Unmatched brace - return error expansion
            analysis->expansion_type = LLE_EXPANSION_ERROR;
            analysis->start_pos = 0;
            analysis->length = length;
            return lle_result_create_success();
        }
        
        analysis->expansion_type = LLE_EXPANSION_PARAMETER;
        analysis->start_pos = 0;
        analysis->length = brace_end + 1;
        
        // Parse parameter expansion content
        return lle_parse_parameter_expansion(grammar, text + 2, brace_end - 2, analysis);
    }
    
    // Command substitution: $(command)
    if (length > 2 && text[1] == '(') {
        size_t paren_end = lle_find_matching_paren(text, 1, length);
        if (paren_end == SIZE_MAX) {
            // Unmatched parenthesis - return error expansion
            analysis->expansion_type = LLE_EXPANSION_ERROR;
            analysis->start_pos = 0;
            analysis->length = length;
            return lle_result_create_success();
        }
        
        analysis->expansion_type = LLE_EXPANSION_COMMAND_SUBSTITUTION;
        analysis->start_pos = 0;
        analysis->length = paren_end + 1;
        analysis->command_content = lle_string_clone_range(grammar->grammar_memory_pool,
                                                           text, 2, paren_end - 2);
        return lle_result_create_success();
    }
    
    // Arithmetic expansion: $((expression))
    if (length > 3 && text[1] == '(' && text[2] == '(') {
        size_t double_paren_end = lle_find_matching_double_paren(text, 1, length);
        if (double_paren_end == SIZE_MAX) {
            // Unmatched parentheses - return error expansion
            analysis->expansion_type = LLE_EXPANSION_ERROR;
            analysis->start_pos = 0;
            analysis->length = length;
            return lle_result_create_success();
        }
        
        analysis->expansion_type = LLE_EXPANSION_ARITHMETIC;
        analysis->start_pos = 0;
        analysis->length = double_paren_end + 1;
        analysis->arithmetic_expr = lle_string_clone_range(grammar->grammar_memory_pool,
                                                           text, 3, double_paren_end - 3);
        return lle_result_create_success();
    }
    
    return lle_result_create_success();
}
```

---

## 7. Display Integration

### 7.1 Lush Display System Integration

```c
// Complete integration with Lush layered display architecture
typedef struct lle_display_integration {
    // Lush display system components
    lle_display_controller_t *display_controller;      // Main display controller
    lle_prompt_layer_t *prompt_layer;                  // Prompt rendering layer
    lle_input_layer_t *input_layer;                    // Input line rendering layer
    lle_syntax_layer_t *syntax_layer;                  // Syntax highlighting layer
    
    // Rendering coordination
    lle_render_coordinator_t *render_coordinator;      // Cross-layer rendering coordination
    lle_update_scheduler_t *update_scheduler;          // Display update scheduling
    lle_invalidation_tracker_t *invalidation;          // Display invalidation tracking
    
    // Theme integration
    lle_theme_coordinator_t *theme_coordinator;        // Theme system coordination
    lle_color_synchronizer_t *color_sync;              // Color synchronization with themes
    
    // Performance optimization
    lle_display_cache_t *display_cache;                // Display rendering cache
    lle_dirty_region_t *dirty_regions;                 // Dirty region tracking
    
    // Memory management
    memory_pool_t *display_memory_pool;                // Display-specific memory pool
} lle_display_integration_t;

// Real-time syntax highlighting display update
lle_result_t lle_display_update_syntax_highlighting(lle_display_integration_t *display,
                                                    const lle_token_list_t *tokens,
                                                    const lle_buffer_region_t *region) {
    if (!display || !tokens || !region) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid parameters for display update");
    }
    
    // Create syntax highlighting display data
    lle_syntax_display_data_t *display_data = 
        lle_syntax_display_data_create(display->display_memory_pool);
    if (!display_data) {
        return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                       "Failed to allocate syntax display data");
    }
    
    // Build display segments with color information
    for (size_t i = 0; i < tokens->count; i++) {
        lle_token_t *token = tokens->tokens[i];
        
        // Skip tokens outside the update region
        if (token->position < region->start || 
            token->position >= region->start + region->length) {
            continue;
        }
        
        // Create display segment for token
        lle_display_segment_t *segment = lle_display_segment_create(
            display->display_memory_pool,
            token->content,
            token->length,
            token->position - region->start  // Relative position
        );
        
        if (!segment) {
            lle_syntax_display_data_destroy(display_data);
            return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                           "Failed to allocate display segment");
        }
        
        // Apply color information to segment
        lle_result_t color_result = lle_apply_token_colors(display, token, segment);
        if (!lle_result_is_success(color_result)) {
            lle_display_segment_destroy(segment);
            lle_syntax_display_data_destroy(display_data);
            return color_result;
        }
        
        // Add segment to display data
        lle_result_t add_result = lle_syntax_display_data_add_segment(display_data, segment);
        if (!lle_result_is_success(add_result)) {
            lle_display_segment_destroy(segment);
            lle_syntax_display_data_destroy(display_data);
            return add_result;
        }
    }
    
    // Submit display data to syntax layer
    lle_result_t render_result = lle_syntax_layer_update(display->syntax_layer,
                                                         display_data,
                                                         region);
    
    lle_syntax_display_data_destroy(display_data);
    return render_result;
}
```

### 7.2 Performance Monitoring and Metrics

```c
// Comprehensive performance monitoring system
typedef struct lle_syntax_metrics {
    // Timing metrics
    double total_analysis_time;                        // Total syntax analysis time (ms)
    double average_analysis_time;                      // Average analysis time per update
    double max_analysis_time;                          // Maximum recorded analysis time
    double min_analysis_time;                          // Minimum recorded analysis time
    
    // Operation counts
    size_t total_analyses;                             // Total number of syntax analyses
    size_t incremental_analyses;                       // Incremental analysis count
    size_t full_analyses;                              // Full buffer analysis count
    size_t cache_hits;                                 // Syntax cache hit count
    size_t cache_misses;                               // Syntax cache miss count
    
    // Performance targets
    double target_analysis_time;                       // Target analysis time (0.5ms)
    double target_cache_hit_rate;                      // Target cache hit rate (75%)
    
    // Memory usage
    size_t peak_memory_usage;                          // Peak memory usage in bytes
    size_t current_memory_usage;                       // Current memory usage
    size_t total_allocations;                          // Total memory allocations
    
    // Error tracking
    size_t syntax_errors_detected;                     // Total syntax errors found
    size_t parsing_failures;                           // Parsing failure count
    size_t recovery_successes;                         // Error recovery success count
    
    // Performance history
    lle_performance_history_t *history;                // Historical performance data
    lle_performance_trend_t *trends;                   // Performance trend analysis
} lle_syntax_metrics_t;

// Performance monitoring and reporting
lle_result_t lle_syntax_metrics_record_analysis(lle_syntax_metrics_t *metrics,
                                               double analysis_time,
                                               lle_analysis_type_t analysis_type,
                                               size_t tokens_processed) {
    if (!metrics) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid metrics parameter");
    }
    
    // Update timing metrics
    metrics->total_analysis_time += analysis_time;
    metrics->total_analyses++;
    
    if (analysis_time > metrics->max_analysis_time) {
        metrics->max_analysis_time = analysis_time;
    }
    
    if (metrics->min_analysis_time == 0.0 || analysis_time < metrics->min_analysis_time) {
        metrics->min_analysis_time = analysis_time;
    }
    
    metrics->average_analysis_time = metrics->total_analysis_time / metrics->total_analyses;
    
    // Update operation counts
    if (analysis_type == LLE_ANALYSIS_INCREMENTAL) {
        metrics->incremental_analyses++;
    } else {
        metrics->full_analyses++;
    }
    
    // Check performance targets
    if (analysis_time > metrics->target_analysis_time) {
        lle_log_warning("Syntax analysis exceeded performance target: %.2f ms (target: %.2f ms)",
                        analysis_time, metrics->target_analysis_time);
    }
    
    // Record performance history
    lle_performance_record_t record = {
        .timestamp = lle_get_current_time(),
        .analysis_time = analysis_time,
        .analysis_type = analysis_type,
        .tokens_processed = tokens_processed
    };
    
    return lle_performance_history_add(metrics->history, &record);
}
```

---

## 7. Error Handling and Recovery

### 8.1 Comprehensive Error Handling System

```c
// Comprehensive error handling and recovery system
typedef struct lle_syntax_error_handler {
    // Error detection and classification
    lle_error_detector_t *error_detector;              // Syntax error detection engine
    lle_error_classifier_t *error_classifier;          // Error type classification
    lle_error_recovery_t *recovery_engine;             // Error recovery strategies
    
    // Error reporting
    lle_error_reporter_t *error_reporter;              // Error reporting and logging
    lle_user_feedback_t *user_feedback;                // User-facing error feedback
    
    // Recovery strategies
    lle_panic_recovery_t *panic_recovery;              // Panic mode recovery
    lle_incremental_recovery_t *incremental_recovery;  // Incremental recovery
    lle_fallback_system_t *fallback_system;            // Fallback highlighting system
    
    // Error history
    lle_error_history_t *error_history;                // Error history tracking
    lle_pattern_analyzer_t *pattern_analyzer;          // Error pattern analysis
    
    // Configuration
    lle_error_config_t *config;                        // Error handling configuration
    bool strict_mode;                                   // Strict error handling mode
} lle_syntax_error_handler_t;

// Robust error detection and recovery
lle_result_t lle_handle_syntax_error(lle_syntax_error_handler_t *handler,
                                     lle_syntax_error_t *error,
                                     lle_token_list_t *tokens,
                                     lle_recovery_action_t **recovery_action) {
    if (!handler || !error || !tokens || !recovery_action) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid parameters for error handling");
    }
    
    // Classify the error type
    lle_error_type_t error_type = lle_classify_syntax_error(handler->error_classifier, error);
    
    // Select appropriate recovery strategy
    lle_recovery_strategy_t strategy;
    switch (error_type) {
        case LLE_ERROR_UNMATCHED_QUOTE:
            strategy = LLE_RECOVERY_QUOTE_COMPLETION;
            break;
            
        case LLE_ERROR_UNMATCHED_BRACKET:
            strategy = LLE_RECOVERY_BRACKET_COMPLETION;
            break;
            
        case LLE_ERROR_INVALID_OPERATOR:
            strategy = LLE_RECOVERY_OPERATOR_CORRECTION;
            break;
            
        case LLE_ERROR_MALFORMED_EXPANSION:
            strategy = LLE_RECOVERY_EXPANSION_FALLBACK;
            break;
            
        case LLE_ERROR_UNKNOWN_CONSTRUCT:
            strategy = LLE_RECOVERY_GENERIC_FALLBACK;
            break;
            
        default:
            strategy = LLE_RECOVERY_PANIC_MODE;
            break;
    }
    
    // Execute recovery strategy
    lle_result_t recovery_result = lle_execute_recovery_strategy(
        handler->recovery_engine,
        strategy,
        error,
        tokens,
        recovery_action
    );
    
    if (!lle_result_is_success(recovery_result)) {
        // Recovery failed - fall back to panic mode
        return lle_execute_panic_recovery(handler->panic_recovery, error, tokens, recovery_action);
    }
    
    // Record error and recovery for pattern analysis
    lle_error_record_t record = {
        .error_type = error_type,
        .recovery_strategy = strategy,
        .recovery_success = true,
        .timestamp = lle_get_current_time(),
        .context = error->context
    };
    
    lle_error_history_add(handler->error_history, &record);
    
    return recovery_result;
}

// Graceful degradation system for severe errors
lle_result_t lle_activate_fallback_highlighting(lle_syntax_error_handler_t *handler,
                                               lle_token_list_t *tokens) {
    if (!handler || !tokens) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid parameters for fallback highlighting");
    }
    
    lle_log_info("Activating fallback highlighting due to syntax errors");
    
    // Apply basic token classification without complex analysis
    for (size_t i = 0; i < tokens->count; i++) {
        lle_token_t *token = tokens->tokens[i];
        
        // Reset any error classifications
        if (token->type >= LLE_TOKEN_ERROR_SYNTAX) {
            token->type = LLE_TOKEN_WORD;
        }
        
        // Apply simple pattern-based classification
        if (lle_is_simple_keyword(token->content)) {
            token->type = LLE_TOKEN_KEYWORD;
        } else if (token->content[0] == '#') {
            token->type = LLE_TOKEN_COMMENT;
        } else if (token->content[0] == '"' || token->content[0] == '\'') {
            token->type = LLE_TOKEN_STRING_SINGLE;  // Generic string
        } else if (token->content[0] == '$') {
            token->type = LLE_TOKEN_VARIABLE;
        }
    }
    
    return lle_result_create_success();
}
```

---

## 8. Testing and Validation

### 9.1 Comprehensive Testing Framework

```c
// Complete testing framework for syntax highlighting system
typedef struct lle_syntax_test_suite {
    // Test categories
    lle_unit_tests_t *unit_tests;                      // Unit tests for individual components
    lle_integration_tests_t *integration_tests;        // Integration tests with Lush
    lle_performance_tests_t *performance_tests;        // Performance and timing tests
    lle_regression_tests_t *regression_tests;          // Regression testing suite
    
    // Test data and fixtures
    lle_test_fixtures_t *test_fixtures;                // Standard test fixtures
    lle_shell_samples_t *shell_samples;                // Real-world shell command samples
    lle_edge_cases_t *edge_cases;                      // Edge case test scenarios
    
    // Validation systems
    lle_token_validator_t *token_validator;            // Token classification validation
    lle_color_validator_t *color_validator;            // Color application validation
    lle_performance_validator_t *perf_validator;       // Performance requirement validation
    
    // Test results
    lle_test_results_t *results;                       // Comprehensive test results
    lle_coverage_analyzer_t *coverage;                 // Code coverage analysis
    
    // Memory management
    memory_pool_t *test_memory_pool;                   // Test-specific memory pool
} lle_syntax_test_suite_t;

// Comprehensive syntax highlighting validation
lle_result_t lle_test_syntax_highlighting_accuracy(lle_syntax_test_suite_t *test_suite,
                                                   const char *test_input,
                                                   const lle_expected_tokens_t *expected) {
    if (!test_suite || !test_input || !expected) {
        return lle_result_create_error(LLE_ERROR_INVALID_PARAMETER,
                                       "Invalid parameters for syntax test");
    }
    
    // Create test syntax analyzer
    lle_syntax_analyzer_t *analyzer = lle_syntax_analyzer_create(test_suite->test_memory_pool);
    if (!analyzer) {
        return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                       "Failed to create test syntax analyzer");
    }
    
    // Create test buffer
    lle_buffer_t *test_buffer = lle_buffer_create_from_string(test_suite->test_memory_pool,
                                                              test_input);
    if (!test_buffer) {
        lle_syntax_analyzer_destroy(analyzer);
        return lle_result_create_error(LLE_ERROR_MEMORY_ALLOCATION,
                                       "Failed to create test buffer");
    }
    
    // Perform syntax analysis
    lle_token_list_t *actual_tokens = NULL;
    lle_result_t analysis_result = lle_syntax_analyze_buffer(analyzer, test_buffer, &actual_tokens);
    
    if (!lle_result_is_success(analysis_result)) {
        lle_buffer_destroy(test_buffer);
        lle_syntax_analyzer_destroy(analyzer);
        return analysis_result;
    }
    
    // Compare actual tokens with expected tokens
    lle_test_comparison_t comparison;
    lle_result_t compare_result = lle_compare_token_lists(actual_tokens, 
                                                          expected->tokens,
                                                          &comparison);
    
    if (!lle_result_is_success(compare_result)) {
        lle_token_list_destroy(actual_tokens);
        lle_buffer_destroy(test_buffer);
        lle_syntax_analyzer_destroy(analyzer);
        return compare_result;
    }
    
    // Record test results
    lle_test_result_t result = {
        .test_name = "syntax_highlighting_accuracy",
        .input = test_input,
        .expected_count = expected->count,
        .actual_count = actual_tokens->count,
        .matches = comparison.matches,
        .mismatches = comparison.mismatches,
        .success = (comparison.mismatches == 0)
    };
    
    lle_test_results_add(test_suite->results, &result);
    
    // Clean up
    lle_token_list_destroy(actual_tokens);
    lle_buffer_destroy(test_buffer);
    lle_syntax_analyzer_destroy(analyzer);
    
    if (result.success) {
        return lle_result_create_success();
    } else {
        return lle_result_create_error(LLE_ERROR_VALIDATION_FAILED,
                                       "Token classification validation failed");
    }
}
```

---

## 9. Implementation Roadmap

### 16.1 Integration-Enhanced Development Phases

#### Phase 1: Integration Foundation (Weeks 1-2)
**Priority**: Critical integration architecture setup

**Week 1: Widget Hook Integration**
1. **Widget Integration Architecture**
   - Implement `lle_widget_hook_integration_t` structure
   - Create `lle_widget_highlight_engine_t` system
   - Set up widget callback registration
   - **Dependencies**: 24_advanced_prompt_widget_hooks_complete.md
   - **Success Criteria**: Widget callbacks functional with <15us mode switching

2. **Bottom-Prompt Highlighting**
   - Implement bottom-prompt syntax parser
   - Create historical command syntax analyzer
   - Build prompt state preservation system
   - **Testing**: Bottom-prompt highlighting accuracy >95%

**Week 2: Adaptive Terminal Integration**
1. **Terminal Integration Architecture**
   - Implement `lle_adaptive_terminal_integration_t` structure
   - Create color capability detection system
   - Set up adaptive color management
   - **Dependencies**: 26_adaptive_terminal_integration_complete.md
   - **Success Criteria**: Terminal detection <50us

2. **Color Adaptation System**
   - Implement terminal-specific color mapping
   - Create graceful fallback mechanisms
   - Build color optimization engine
   - **Testing**: Color adaptation works across all terminal types

#### Phase 2: Enhanced Highlighting Intelligence (Weeks 3-4)
**Priority**: Advanced highlighting capabilities with integration

**Week 3: Cross-System Context**
1. **Unified Context Engine**
   - Upgrade syntax analyzer with widget context data
   - Implement cross-system context sharing
   - Create context consistency validation
   - **Integration**: Share highlighting context with widget and terminal systems
   - **Performance**: Context updates <30us

2. **Enhanced Color Management**
   - Implement integration-aware color selection
   - Create widget-specific color schemes
   - Build terminal-optimized color algorithms
   - **Testing**: Color consistency across all integration modes

**Week 4: Performance Integration**
1. **Integration Performance Optimization**
   - Optimize widget callback overhead <25us
   - Implement integration-specific caching
   - Create performance monitoring for integrations
   - **Performance**: Total integration overhead <100us

2. **Integration Testing Framework**
   - Build cross-system integration tests
   - Create performance regression testing
   - Implement integration validation suite
   - **Success Criteria**: All integration tests pass

### 16.2 Development Phases (Original + Integration Enhancements)

**Phase 1: Core Syntax Analysis Engine (Weeks 1-2)**
- Implement lexical tokenizer with comprehensive shell language support
- Develop token classification system with context awareness
- Create syntax validation and error detection system
- Basic testing framework implementation

**Phase 2: Real-Time Highlighting System (Weeks 3-4)**
- Implement real-time update processing with change detection
- Develop incremental parsing system for performance
- Create performance optimization and caching systems
- Integration with LLE buffer management system

**Phase 3: Color Management and Display Integration (Weeks 5-6)**
- Implement dynamic color system with theme integration
- Develop terminal color capability detection
- Create Lush display system integration
- Visual effects and formatting implementation

**Phase 4: Advanced Features and Optimization (Weeks 7-8)**
- Implement shell language grammar extensions
- Advanced error handling and recovery systems
- Performance tuning and optimization
- Memory pool integration completion

**Phase 5: Testing and Validation (Weeks 9-10)**
- Comprehensive test suite development
- Performance benchmarking and validation
- Integration testing with complete Lush system
- Documentation and deployment preparation

### 10.2 Success Criteria

- **Sub-millisecond Performance**: All syntax highlighting updates complete within 0.5ms
- **Accuracy Target**: 99.5% token classification accuracy on real-world shell commands
- **Memory Efficiency**: Zero-allocation operation during steady-state highlighting
- **Error Resilience**: Graceful handling of all syntax error conditions
- **Theme Integration**: Seamless integration with all Lush themes
- **Universal Compatibility**: Consistent operation across all supported terminal types

### 10.3 Integration Points

- **LLE Buffer System**: Real-time buffer change notifications and content access
- **LLE Event System**: Event-driven highlighting updates with priority handling
- **Lush Display System**: Layered display integration for visual presentation
- **Lush Theme System**: Dynamic color scheme integration and adaptation
- **Lush Memory Pool**: Zero-allocation operation with memory pool integration
- **LLE Configuration**: User customization and preference management

---

## Conclusion

This comprehensive specification provides implementation-ready details for the LLE Syntax Highlighting System, ensuring real-time visual enhancement of shell commands while maintaining enterprise-grade performance and reliability. The system integrates seamlessly with existing Lush architecture and provides the foundation for advanced features like intelligent autosuggestions and context-aware completion.

The specification achieves the project's goal of microscopic implementation detail, enabling virtually guaranteed development success through comprehensive coverage of algorithms, error handling, performance requirements, and integration specifications.