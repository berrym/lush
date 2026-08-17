# History-Buffer Integration Complete Specification

**Document**: 22_history_buffer_integration_complete.md  
**Version**: 1.0.0  
**Date**: 2025-10-10  
**Status**: Implementation-Ready Specification  
**Classification**: Critical Core Integration Component  

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Interactive History Editing System](#3-interactive-history-editing-system)
4. [Multiline Command Recall and Restoration](#4-multiline-command-recall-and-restoration)
5. [Buffer-History Integration Layer](#5-buffer-history-integration-layer)
6. [Command Reconstruction Engine](#6-command-reconstruction-engine)
7. [Callback-Based Editing Framework](#7-callback-based-editing-framework)
8. [Performance and Memory Management](#8-performance-and-memory-management)
9. [Event System Integration](#9-event-system-integration)
10. [Error Handling and Recovery](#10-error-handling-and-recovery)
11. [Testing and Validation](#11-testing-and-validation)
12. [Implementation Roadmap](#12-implementation-roadmap)

---

## 1. Executive Summary

### 1.1 Purpose

The History-Buffer Integration system implements the critical missing functionality from the LLE_DESIGN_DOCUMENT.md requirements: seamless integration between the history system and buffer management for interactive editing of historical commands, with complete multiline structure preservation and restoration capabilities.

### 1.2 Key Features

- **Interactive History Editing**: Complete implementation of `lle_history_edit_entry()` with callback-based editing system
- **Multiline Command Recall**: Full preservation and restoration of complex shell constructs (loops, conditionals, functions)
- **Original Structure Preservation**: Implementation of `original_multiline` field preservation from design document
- **Buffer Integration**: Seamless loading of historical commands into editing buffer with complete structural restoration
- **Logical Unit Editing**: Commands treated as complete logical units with semantic structure awareness
- **Advanced Reconstruction**: Intelligent restoration of multiline formatting with proper indentation and structure

### 1.3 Critical Design Implementation

This specification directly implements the missing core functionality from LLE_DESIGN_DOCUMENT.md:

1. **`lle_history_entry_t.original_multiline`** preservation system
2. **`lle_history_edit_entry()`** interactive editing with callbacks
3. **Buffer-oriented multiline command restoration** 
4. **Logical unit editing** for complex shell constructs
5. **Seamless integration** between history storage and buffer editing

---

## 2. Architecture Overview

### 2.1 Core Integration System

```c
// Primary history-buffer integration system
typedef struct lle_history_buffer_integration {
    // Core components
    lle_history_system_t *history_system;             // History system reference
    lle_buffer_t *editing_buffer;                     // Buffer system reference
    lle_reconstruction_engine_t *reconstruction;     // Command reconstruction engine
    lle_edit_session_manager_t *session_manager;     // Edit session management
    
    // Multiline support
    lle_multiline_parser_t *multiline_parser;        // Multiline structure parser
    lle_structure_analyzer_t *structure_analyzer;    // Shell construct analyzer
    lle_formatting_engine_t *formatter;              // Intelligent formatting engine
    
    // Callback system
    lle_history_edit_callbacks_t *edit_callbacks;    // Edit event callbacks
    lle_callback_registry_t *callback_registry;      // Callback management
    
    // Performance optimization
    lle_edit_cache_t *edit_cache;                     // Edit operation caching
    lle_memory_pool_t *memory_pool;                   // Memory pool integration
    lle_performance_metrics_t *metrics;               // Performance monitoring
    
    // Configuration and state
    lle_integration_config_t *config;                // Integration configuration
    lle_integration_state_t *current_state;          // Current integration state
    
    // Synchronization
    pthread_rwlock_t integration_lock;               // Thread-safe access
    bool system_active;                              // Integration system status
    uint64_t session_counter;                        // Edit session counter
} lle_history_buffer_integration_t;
```

### 2.2 History Entry Structure Implementation

```c
// Complete implementation of LLE_DESIGN_DOCUMENT.md history entry
typedef struct lle_history_entry {
    // Core entry data
    uint64_t entry_id;                                // Unique entry identifier
    char *command;                                    // Normalized command text
    char *original_multiline;                         // CRITICAL: Original multiline formatting preserved
    bool is_multiline;                                // Multiline structure flag
    size_t command_length;                            // Command text length
    size_t original_length;                           // Original multiline length
    
    // Structural information for reconstruction
    lle_command_structure_t *structure_info;          // Shell construct structure
    lle_indentation_info_t *indentation;             // Original indentation patterns
    lle_line_mapping_t *line_mapping;                // Line boundary mapping
    
    // Execution context
    uint64_t timestamp;                               // Command execution timestamp
    uint32_t duration_ms;                             // Execution duration
    int exit_code;                                    // Command exit status
    char *working_directory;                          // Execution directory
    session_id_t session_id;                          // Session identifier
    
    // Edit history tracking
    uint32_t edit_count;                              // Number of times edited
    uint64_t last_edited;                             // Last edit timestamp
    bool has_been_edited;                             // Edit history flag
    
    // Classification and metadata
    lle_command_type_t command_type;                  // Command classification
    bool is_private;                                  // Sensitive command flag
    uint32_t access_count;                            // Access frequency
    uint64_t last_accessed;                           // Last access time
} lle_history_entry_t;
```

---

## 3. Interactive History Editing System

### 3.1 Callback-Based Editing Framework

```c
// Direct implementation of LLE_DESIGN_DOCUMENT.md callback system
typedef struct lle_history_edit_callbacks {
    lle_result_t (*on_edit_start)(lle_history_entry_t *entry, void *user_data);
    lle_result_t (*on_edit_complete)(lle_history_entry_t *entry, void *user_data);
    lle_result_t (*on_edit_cancel)(lle_history_entry_t *entry, void *user_data);
    
    // Extended callbacks for advanced editing
    lle_result_t (*on_buffer_loaded)(lle_buffer_t *buffer, lle_history_entry_t *entry, void *user_data);
    lle_result_t (*on_structure_reconstructed)(lle_multiline_info_t *multiline, void *user_data);
    lle_result_t (*on_edit_modified)(lle_buffer_t *buffer, lle_edit_change_t *change, void *user_data);
    lle_result_t (*on_save_requested)(lle_buffer_t *buffer, lle_history_entry_t *entry, void *user_data);
    
    void *user_data;                                  // User-provided context data
} lle_history_edit_callbacks_t;

// Core interactive editing function - direct from design document
lle_result_t lle_history_edit_entry(lle_history_buffer_integration_t *integration,
                                    size_t entry_index,
                                    lle_history_edit_callbacks_t *callbacks,
                                    void *user_data) {
    if (!integration || !callbacks || !integration->system_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    pthread_rwlock_wrlock(&integration->integration_lock);
    
    // Step 1: Retrieve history entry
    lle_history_entry_t *entry = NULL;
    lle_result_t result = lle_history_system_get_entry(
        integration->history_system, entry_index, &entry);
    
    if (result != LLE_SUCCESS || !entry) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return result;
    }
    
    // Step 2: Create edit session
    lle_edit_session_t *session = NULL;
    result = lle_edit_session_manager_create_session(
        integration->session_manager, entry, &session);
    
    if (result != LLE_SUCCESS) {
        pthread_rwlock_unlock(&integration->integration_lock);
        return result;
    }
    
    // Step 3: Invoke on_edit_start callback
    if (callbacks->on_edit_start) {
        result = callbacks->on_edit_start(entry, user_data);
        if (result != LLE_SUCCESS) {
            lle_edit_session_manager_destroy_session(integration->session_manager, session);
            pthread_rwlock_unlock(&integration->integration_lock);
            return result;
        }
    }
    
    // Step 4: Load entry into buffer with complete multiline restoration
    result = lle_history_buffer_load_entry_with_reconstruction(
        integration, entry, integration->editing_buffer, callbacks);
    
    if (result != LLE_SUCCESS) {
        if (callbacks->on_edit_cancel) {
            callbacks->on_edit_cancel(entry, user_data);
        }
        lle_edit_session_manager_destroy_session(integration->session_manager, session);
        pthread_rwlock_unlock(&integration->integration_lock);
        return result;
    }
    
    // Step 5: Set up edit monitoring and callbacks
    session->callbacks = callbacks;
    session->user_data = user_data;
    session->buffer = integration->editing_buffer;
    session->active = true;
    
    // Step 6: Register session for completion handling
    integration->current_state->active_session = session;
    
    pthread_rwlock_unlock(&integration->integration_lock);
    
    // Edit session is now active - completion handled through events
    return LLE_SUCCESS;
}
```

### 3.2 Edit Session Management

```c
// Complete edit session lifecycle management
typedef struct lle_edit_session {
    uint64_t session_id;                              // Unique session identifier
    lle_history_entry_t *original_entry;             // Original history entry
    lle_buffer_t *editing_buffer;                     // Buffer being edited
    lle_history_edit_callbacks_t *callbacks;         // Session callbacks
    void *user_data;                                  // User context data
    
    // Session state
    bool active;                                      // Session active flag
    uint64_t start_time;                              // Session start timestamp
    uint64_t last_activity;                           // Last edit activity
    
    // Edit tracking
    lle_change_list_t *changes;                       // List of edits made
    size_t change_count;                              // Number of changes
    bool has_modifications;                           // Modification flag
    
    // Multiline reconstruction context
    lle_multiline_context_t *multiline_context;      // Multiline editing context
    lle_structure_preservation_t *structure_state;   // Structure preservation state
    
    // Memory management
    lle_memory_pool_t *session_memory_pool;          // Session-specific memory pool
} lle_edit_session_t;

// Session completion handling
lle_result_t lle_edit_session_complete(lle_history_buffer_integration_t *integration,
                                       lle_edit_session_t *session,
                                       bool save_changes) {
    if (!integration || !session || !session->active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    pthread_rwlock_wrlock(&integration->integration_lock);
    
    lle_result_t result = LLE_SUCCESS;
    
    if (save_changes && session->has_modifications) {
        // Step 1: Extract modified content from buffer
        char *modified_content = NULL;
        size_t content_length = 0;
        
        result = lle_buffer_get_complete_content(
            session->editing_buffer, &modified_content, &content_length);
        
        if (result == LLE_SUCCESS && modified_content) {
            // Step 2: Preserve multiline structure if needed
            char *preserved_multiline = NULL;
            if (lle_buffer_is_multiline(session->editing_buffer)) {
                result = lle_multiline_parser_preserve_structure(
                    integration->multiline_parser,
                    session->editing_buffer,
                    &preserved_multiline);
            }
            
            // Step 3: Update history entry
            if (result == LLE_SUCCESS) {
                lle_history_entry_t *updated_entry = lle_history_entry_create_modified(
                    session->original_entry,
                    modified_content,
                    preserved_multiline,
                    integration->memory_pool);
                
                if (updated_entry) {
                    // Step 4: Replace entry in history
                    result = lle_history_system_replace_entry(
                        integration->history_system,
                        session->original_entry->entry_id,
                        updated_entry);
                    
                    // Step 5: Invoke completion callback
                    if (result == LLE_SUCCESS && session->callbacks->on_edit_complete) {
                        session->callbacks->on_edit_complete(updated_entry, session->user_data);
                    }
                } else {
                    result = LLE_ERROR_MEMORY_ALLOCATION;
                }
            }
            
            // Cleanup
            if (modified_content) {
                lle_memory_pool_free(integration->memory_pool, modified_content);
            }
            if (preserved_multiline) {
                lle_memory_pool_free(integration->memory_pool, preserved_multiline);
            }
        }
    } else {
        // Edit cancelled
        if (session->callbacks->on_edit_cancel) {
            session->callbacks->on_edit_cancel(session->original_entry, session->user_data);
        }
    }
    
    // Clean up session
    session->active = false;
    integration->current_state->active_session = NULL;
    
    // Clear buffer
    lle_buffer_clear(session->editing_buffer);
    
    // Destroy session
    lle_edit_session_manager_destroy_session(integration->session_manager, session);
    
    pthread_rwlock_unlock(&integration->integration_lock);
    
    return result;
}
```

---

## 4. Multiline Command Recall and Restoration

### 4.1 Command Structure Analysis

```c
// Shell construct structure analysis for intelligent reconstruction
typedef enum lle_command_construct_type {
    LLE_CONSTRUCT_SIMPLE,                             // Simple single command
    LLE_CONSTRUCT_PIPELINE,                           // Command pipeline
    LLE_CONSTRUCT_FOR_LOOP,                           // For loop construct
    LLE_CONSTRUCT_WHILE_LOOP,                         // While loop construct
    LLE_CONSTRUCT_IF_STATEMENT,                       // If/then/else statement
    LLE_CONSTRUCT_CASE_STATEMENT,                     // Case statement
    LLE_CONSTRUCT_FUNCTION_DEF,                       // Function definition
    LLE_CONSTRUCT_SUBSHELL,                           // Subshell execution
    LLE_CONSTRUCT_COMMAND_SUBSTITUTION,               // Command substitution
    LLE_CONSTRUCT_HEREDOC,                            // Here document
    LLE_CONSTRUCT_COMPOUND,                           // Compound statement
    LLE_CONSTRUCT_CONDITIONAL                         // Conditional execution
} lle_command_construct_type_t;

typedef struct lle_command_structure {
    lle_command_construct_type_t construct_type;      // Type of shell construct
    
    // Structure boundaries
    size_t start_offset;                              // Start position in original text
    size_t end_offset;                                // End position in original text
    size_t line_start;                                // Starting line number
    size_t line_end;                                  // Ending line number
    
    // Indentation and formatting
    lle_indentation_pattern_t *indentation;          // Indentation pattern
    lle_line_continuation_t *continuations;          // Line continuation info
    size_t base_indent_level;                         // Base indentation level
    
    // Nested structures
    struct lle_command_structure **nested_constructs; // Nested command structures
    size_t nested_count;                              // Number of nested constructs
    
    // Keywords and syntax elements
    lle_keyword_position_t *keywords;                // Positions of shell keywords
    size_t keyword_count;                             // Number of keywords
    
    // Context information
    bool requires_completion;                         // Needs completion (like 'done', 'fi')
    char **completion_keywords;                       // Required completion keywords
    size_t completion_count;                          // Number of completion keywords needed
} lle_command_structure_t;
```

### 4.2 Multiline Reconstruction Engine

```c
// Complete multiline command reconstruction with structure preservation
lle_result_t lle_history_buffer_load_entry_with_reconstruction(
    lle_history_buffer_integration_t *integration,
    lle_history_entry_t *entry,
    lle_buffer_t *target_buffer,
    lle_history_edit_callbacks_t *callbacks) {
    
    if (!integration || !entry || !target_buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    lle_result_t result = LLE_SUCCESS;
    
    // Step 1: Clear target buffer
    result = lle_buffer_clear(target_buffer);
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Step 2: Determine source content (prioritize original_multiline)
    const char *source_content = NULL;
    size_t source_length = 0;
    
    if (entry->original_multiline && strlen(entry->original_multiline) > 0) {
        // Use preserved original multiline formatting
        source_content = entry->original_multiline;
        source_length = entry->original_length;
    } else {
        // Fall back to normalized command
        source_content = entry->command;
        source_length = entry->command_length;
    }
    
    // Step 3: Analyze command structure for intelligent reconstruction
    lle_command_structure_t *structure = NULL;
    if (entry->is_multiline) {
        result = lle_structure_analyzer_analyze_command(
            integration->structure_analyzer,
            source_content,
            source_length,
            &structure);
        
        if (result != LLE_SUCCESS) {
            // Continue with simple loading if structure analysis fails
            structure = NULL;
        }
    }
    
    // Step 4: Reconstruct multiline content with proper formatting
    char *reconstructed_content = NULL;
    size_t reconstructed_length = 0;
    
    if (structure && entry->is_multiline) {
        result = lle_reconstruction_engine_reconstruct_multiline(
            integration->reconstruction,
            source_content,
            source_length,
            structure,
            &reconstructed_content,
            &reconstructed_length);
    } else {
        // Simple content loading
        reconstructed_content = lle_string_duplicate(source_content, integration->memory_pool);
        reconstructed_length = source_length;
        result = reconstructed_content ? LLE_SUCCESS : LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Step 5: Load content into buffer
    result = lle_buffer_set_content(target_buffer, reconstructed_content, reconstructed_length);
    if (result != LLE_SUCCESS) {
        lle_memory_pool_free(integration->memory_pool, reconstructed_content);
        return result;
    }
    
    // Step 6: Set up multiline structure in buffer
    if (entry->is_multiline && structure) {
        result = lle_buffer_apply_multiline_structure(target_buffer, structure);
        if (result != LLE_SUCCESS) {
            lle_memory_pool_free(integration->memory_pool, reconstructed_content);
            return result;
        }
    }
    
    // Step 7: Position cursor appropriately
    if (entry->is_multiline) {
        // Position at end of content for multiline editing
        result = lle_buffer_move_cursor_to_buffer_end(target_buffer);
    } else {
        // Position at start for simple commands
        result = lle_buffer_move_cursor_to_buffer_start(target_buffer);
    }
    
    // Step 8: Invoke buffer loaded callback
    if (result == LLE_SUCCESS && callbacks && callbacks->on_buffer_loaded) {
        result = callbacks->on_buffer_loaded(target_buffer, entry, callbacks->user_data);
    }
    
    // Step 9: Invoke structure reconstructed callback for multiline
    if (result == LLE_SUCCESS && entry->is_multiline && structure && 
        callbacks && callbacks->on_structure_reconstructed) {
        
        lle_multiline_info_t multiline_info = {
            .structure = structure,
            .original_content = source_content,
            .reconstructed_content = reconstructed_content,
            .is_complete = true
        };
        
        result = callbacks->on_structure_reconstructed(&multiline_info, callbacks->user_data);
    }
    
    // Cleanup
    lle_memory_pool_free(integration->memory_pool, reconstructed_content);
    if (structure) {
        lle_command_structure_destroy(structure);
    }
    
    return result;
}
```

### 4.3 Intelligent Structure Reconstruction

```c
// Advanced multiline command reconstruction with intelligent formatting
lle_result_t lle_reconstruction_engine_reconstruct_multiline(
    lle_reconstruction_engine_t *engine,
    const char *source_content,
    size_t source_length,
    lle_command_structure_t *structure,
    char **reconstructed_content,
    size_t *reconstructed_length) {
    
    if (!engine || !source_content || !structure || !reconstructed_content) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Step 1: Create reconstruction buffer
    lle_string_builder_t *builder = lle_string_builder_create(
        source_length * 2, engine->memory_pool); // Estimate 2x for formatting
    
    if (!builder) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    lle_result_t result = LLE_SUCCESS;
    const char *current_pos = source_content;
    const char *end_pos = source_content + source_length;
    
    // Step 2: Process each line with intelligent indentation
    size_t current_line = 0;
    size_t current_indent_level = 0;
    
    while (current_pos < end_pos && result == LLE_SUCCESS) {
        const char *line_start = current_pos;
        const char *line_end = strchr(current_pos, '\n');
        
        if (!line_end) {
            line_end = end_pos;
        }
        
        size_t line_length = line_end - line_start;
        
        // Step 3: Determine proper indentation for this line
        size_t target_indent = lle_calculate_target_indentation(
            structure, current_line, current_indent_level);
        
        // Step 4: Apply indentation
        for (size_t i = 0; i < target_indent; i++) {
            result = lle_string_builder_append_char(builder, ' ');
            if (result != LLE_SUCCESS) break;
        }
        
        // Step 5: Add line content (trimming original indentation)
        const char *content_start = line_start;
        while (content_start < line_end && (*content_start == ' ' || *content_start == '\t')) {
            content_start++;
        }
        
        if (content_start < line_end) {
            result = lle_string_builder_append_data(
                builder, content_start, line_end - content_start);
        }
        
        // Step 6: Add newline if not at end
        if (line_end < end_pos && result == LLE_SUCCESS) {
            result = lle_string_builder_append_char(builder, '\n');
        }
        
        // Step 7: Update state for next line
        current_line++;
        current_indent_level = lle_calculate_next_indent_level(
            structure, current_line, target_indent, content_start, line_end - content_start);
        
        // Move to next line
        current_pos = (line_end < end_pos) ? line_end + 1 : end_pos;
    }
    
    // Step 8: Extract final content
    if (result == LLE_SUCCESS) {
        result = lle_string_builder_build(builder, reconstructed_content, reconstructed_length);
    }
    
    lle_string_builder_destroy(builder);
    return result;
}

// Calculate proper indentation based on shell construct structure
size_t lle_calculate_target_indentation(lle_command_structure_t *structure,
                                       size_t line_number,
                                       size_t current_level) {
    if (!structure) {
        return 0;
    }
    
    size_t base_indent = structure->base_indent_level;
    size_t additional_indent = 0;
    
    // Determine additional indentation based on construct type
    switch (structure->construct_type) {
        case LLE_CONSTRUCT_FOR_LOOP:
        case LLE_CONSTRUCT_WHILE_LOOP:
            if (line_number > structure->line_start && line_number < structure->line_end) {
                additional_indent = 2; // Standard 2-space indent inside loops
            }
            break;
            
        case LLE_CONSTRUCT_IF_STATEMENT:
            if (line_number > structure->line_start) {
                if (lle_line_contains_keyword(structure, line_number, "then")) {
                    additional_indent = 0; // 'then' aligns with 'if'
                } else if (lle_line_contains_keyword(structure, line_number, "else") ||
                          lle_line_contains_keyword(structure, line_number, "elif")) {
                    additional_indent = 0; // 'else'/'elif' align with 'if'
                } else if (lle_line_contains_keyword(structure, line_number, "fi")) {
                    additional_indent = 0; // 'fi' aligns with 'if'
                } else {
                    additional_indent = 2; // Content inside if/then/else
                }
            }
            break;
            
        case LLE_CONSTRUCT_FUNCTION_DEF:
            if (line_number > structure->line_start && line_number < structure->line_end) {
                additional_indent = 2; // Function body content
            }
            break;
            
        case LLE_CONSTRUCT_CASE_STATEMENT:
            // Case statements have complex indentation rules
            additional_indent = lle_calculate_case_indentation(structure, line_number);
            break;
            
        default:
            break;
    }
    
    // Check for nested structures
    for (size_t i = 0; i < structure->nested_count; i++) {
        lle_command_structure_t *nested = structure->nested_constructs[i];
        if (line_number >= nested->line_start && line_number <= nested->line_end) {
            additional_indent += lle_calculate_target_indentation(nested, line_number, current_level);
            break; // Only apply the deepest nesting level
        }
    }
    
    return base_indent + additional_indent;
}
```

---

## 5. Buffer-History Integration Layer

### 5.1 Direct Buffer Loading Interface

```c
// Core buffer loading functions implementing design requirements
lle_result_t lle_buffer_load_from_history_entry(lle_buffer_t *buffer,
                                               lle_history_entry_t *entry,
                                               lle_history_buffer_integration_t *integration,
                                               bool preserve_multiline_structure) {
    if (!buffer || !entry || !integration) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    if (preserve_multiline_structure && entry->is_multiline && entry->original_multiline) {
        // Use full reconstruction engine
        return lle_history_buffer_load_entry_with_reconstruction(
            integration, entry, buffer, NULL);
    } else {
        // Simple content loading
        const char *content = entry->original_multiline ? entry->original_multiline : entry->command;
        size_t length = entry->original_multiline ? entry->original_length : entry->command_length;
        
        return lle_buffer_set_content(buffer, content, length);
    }
}

// Quick recall function for immediate editing
lle_result_t lle_history_quick_recall_for_editing(lle_history_buffer_integration_t *integration,
                                                  size_t entry_index,
                                                  lle_buffer_t *target_buffer) {
    if (!integration || !target_buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Retrieve entry
    lle_history_entry_t *entry = NULL;
    lle_result_t result = lle_history_system_get_entry(
        integration->history_system, entry_index, &entry);
    
    if (result != LLE_SUCCESS || !entry) {
        return result;
    }
    
    // Load with full reconstruction
    return lle_buffer_load_from_history_entry(
        target_buffer, entry, integration, true);
}

// Search and recall integration
lle_result_t lle_history_search_and_recall(lle_history_buffer_integration_t *integration,
                                           const char *search_query,
                                           history_search_mode_t search_mode,
                                           size_t result_index,
                                           lle_buffer_t *target_buffer) {
    if (!integration || !search_query || !target_buffer) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Perform search
    lle_search_results_t *search_results = NULL;
    lle_result_t result = lle_history_search_engine_search(
        integration->history_system->search_engine,
        search_query,
        search_mode,
        10, // Max results
        &search_results);
    
    if (result != LLE_SUCCESS || !search_results || 
        result_index >= search_results->count) {
        if (search_results) {
            lle_search_results_destroy(search_results);
        }
        return LLE_ERROR_NOT_FOUND;
    }
    
    // Get selected result
    lle_history_entry_t *selected_entry = search_results->entries[result_index];
    
    // Load into buffer
    result = lle_buffer_load_from_history_entry(
        target_buffer, selected_entry, integration, true);
    
    lle_search_results_destroy(search_results);
    return result;
}
```

### 5.2 Buffer Content Preservation

```c
// Save buffer content back to history with multiline preservation
lle_result_t lle_buffer_save_to_history(lle_buffer_t *buffer,
                                        lle_history_buffer_integration_t *integration,
                                        lle_history_entry_t *original_entry) {
    if (!buffer || !integration || !original_entry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Step 1: Extract current buffer content
    char *current_content = NULL;
    size_t content_length = 0;
    
    lle_result_t result = lle_buffer_get_complete_content(buffer, &current_content, &content_length);
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Step 2: Preserve multiline structure if buffer contains multiline content
    char *preserved_multiline = NULL;
    if (lle_buffer_is_multiline(buffer)) {
        result = lle_multiline_parser_preserve_structure(
            integration->multiline_parser, buffer, &preserved_multiline);
        
        if (result != LLE_SUCCESS) {
            lle_memory_pool_free(integration->memory_pool, current_content);
            return result;
        }
    }
    
    // Step 3: Create updated history entry
    lle_history_entry_t *updated_entry = lle_history_entry_create_modified(
        original_entry,
        current_content,
        preserved_multiline,
        integration->memory_pool);
    
    if (!updated_entry) {
        lle_memory_pool_free(integration->memory_pool, current_content);
        if (preserved_multiline) {
            lle_memory_pool_free(integration->memory_pool, preserved_multiline);
        }
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Step 4: Replace entry in history system
    result = lle_history_system_replace_entry(
        integration->history_system, original_entry->entry_id, updated_entry);
    
    // Cleanup
    lle_memory_pool_free(integration->memory_pool, current_content);
    if (preserved_multiline) {
        lle_memory_pool_free(integration->memory_pool, preserved_multiline);
    }
    
    return result;
}
```

---

## 6. Command Reconstruction Engine

### 6.1 Structure Analysis and Parsing

```c
// Advanced shell construct parsing for multiline commands
lle_result_t lle_structure_analyzer_analyze_command(lle_structure_analyzer_t *analyzer,
                                                   const char *command_text,
                                                   size_t text_length,
                                                   lle_command_structure_t **structure) {
    if (!analyzer || !command_text || !structure) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    *structure = NULL;
    
    // Step 1: Create structure object
    lle_command_structure_t *cmd_structure = lle_memory_pool_alloc(
        analyzer->memory_pool, sizeof(lle_command_structure_t));
    
    if (!cmd_structure) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    memset(cmd_structure, 0, sizeof(lle_command_structure_t));
    
    // Step 2: Tokenize command for structure analysis
    lle_token_list_t *tokens = NULL;
    lle_result_t result = lle_shell_tokenizer_tokenize(
        analyzer->tokenizer, command_text, text_length, &tokens);
    
    if (result != LLE_SUCCESS) {
        lle_memory_pool_free(analyzer->memory_pool, cmd_structure);
        return result;
    }
    
    // Step 3: Detect primary construct type
    result = lle_detect_primary_construct_type(tokens, &cmd_structure->construct_type);
    if (result != LLE_SUCCESS) {
        lle_token_list_destroy(tokens);
        lle_memory_pool_free(analyzer->memory_pool, cmd_structure);
        return result;
    }
    
    // Step 4: Analyze line boundaries and structure
    result = lle_analyze_line_structure(command_text, text_length, cmd_structure);
    if (result != LLE_SUCCESS) {
        lle_token_list_destroy(tokens);
        lle_command_structure_destroy(cmd_structure);
        return result;
    }
    
    // Step 5: Extract indentation patterns
    result = lle_extract_indentation_patterns(command_text, text_length, cmd_structure);
    if (result != LLE_SUCCESS) {
        lle_token_list_destroy(tokens);
        lle_command_structure_destroy(cmd_structure);
        return result;
    }
    
    // Step 6: Identify nested constructs
    result = lle_identify_nested_constructs(tokens, cmd_structure);
    if (result != LLE_SUCCESS) {
        lle_token_list_destroy(tokens);
        lle_command_structure_destroy(cmd_structure);
        return result;
    }
    
    // Step 7: Extract keyword positions for reconstruction
    result = lle_extract_keyword_positions(tokens, cmd_structure);
    if (result != LLE_SUCCESS) {
        lle_token_list_destroy(tokens);
        lle_command_structure_destroy(cmd_structure);
        return result;
    }
    
    lle_token_list_destroy(tokens);
    *structure = cmd_structure;
    
    return LLE_SUCCESS;
}
```

### 6.2 Intelligent Formatting Engine

```c
// Calculate proper indentation for shell constructs
size_t lle_calculate_case_indentation(lle_command_structure_t *structure, size_t line_number) {
    // Case statement indentation rules:
    // case VAR in
    //   pattern1)
    //     commands
    //     ;;
    //   pattern2)
    //     commands
    //     ;;
    // esac
    
    if (!structure || structure->construct_type != LLE_CONSTRUCT_CASE_STATEMENT) {
        return 0;
    }
    
    // Find what type of line this is within the case statement
    for (size_t i = 0; i < structure->keyword_count; i++) {
        lle_keyword_position_t *keyword = &structure->keywords[i];
        
        if (keyword->line_number == line_number) {
            if (strcmp(keyword->keyword, "case") == 0 || strcmp(keyword->keyword, "esac") == 0) {
                return 0; // case/esac at base level
            } else if (strstr(keyword->keyword, ")")) {
                return 2; // Pattern lines indented 2 spaces
            } else if (strcmp(keyword->keyword, ";;") == 0) {
                return 4; // Command terminator indented 4 spaces  
            }
        }
    }
    
    // Default case: command content inside pattern
    return 4; // Commands indented 4 spaces from case
}

// Determine next indentation level based on current line content
size_t lle_calculate_next_indent_level(lle_command_structure_t *structure,
                                      size_t next_line_number,
                                      size_t current_indent,
                                      const char *current_line_content,
                                      size_t content_length) {
    if (!structure || !current_line_content) {
        return current_indent;
    }
    
    // Check if current line changes indentation for next line
    char *line_copy = strndup(current_line_content, content_length);
    if (!line_copy) {
        return current_indent;
    }
    
    // Trim whitespace
    char *trimmed = lle_string_trim(line_copy);
    size_t next_indent = current_indent;
    
    // Analyze construct-specific indentation changes
    switch (structure->construct_type) {
        case LLE_CONSTRUCT_FOR_LOOP:
            if (strstr(trimmed, "for ") && strstr(trimmed, " in ")) {
                next_indent = current_indent + 2; // Indent loop body
            } else if (strstr(trimmed, "done")) {
                next_indent = current_indent - 2; // Unindent after loop
            }
            break;
            
        case LLE_CONSTRUCT_WHILE_LOOP:
            if (strstr(trimmed, "while ")) {
                next_indent = current_indent + 2; // Indent loop body
            } else if (strstr(trimmed, "done")) {
                next_indent = current_indent - 2; // Unindent after loop
            }
            break;
            
        case LLE_CONSTRUCT_IF_STATEMENT:
            if (strstr(trimmed, "if ")) {
                next_indent = current_indent + 2; // Indent then block
            } else if (strstr(trimmed, "then")) {
                // 'then' doesn't change next line indent
            } else if (strstr(trimmed, "else")) {
                next_indent = current_indent + 2; // Indent else block
            } else if (strstr(trimmed, "elif ")) {
                next_indent = current_indent + 2; // Indent elif block
            } else if (strstr(trimmed, "fi")) {
                next_indent = current_indent - 2; // Unindent after if
            }
            break;
            
        case LLE_CONSTRUCT_FUNCTION_DEF:
            if (strstr(trimmed, "() {") || strstr(trimmed, "function ")) {
                next_indent = current_indent + 2; // Indent function body
            } else if (strstr(trimmed, "}")) {
                next_indent = current_indent - 2; // Unindent after function
            }
            break;
            
        default:
            break;
    }
    
    free(line_copy);
    return next_indent > 0 ? next_indent : 0;
}
```

---

## 7. Callback-Based Editing Framework

### 7.1 Event Integration for History Editing

```c
// Event types for history-buffer integration
typedef enum lle_history_buffer_event_type {
    LLE_HIST_BUF_EVENT_EDIT_START,                    // History edit session started
    LLE_HIST_BUF_EVENT_EDIT_COMPLETE,                // History edit completed
    LLE_HIST_BUF_EVENT_EDIT_CANCEL,                  // History edit cancelled
    LLE_HIST_BUF_EVENT_BUFFER_LOADED,                // Buffer loaded from history
    LLE_HIST_BUF_EVENT_STRUCTURE_RECONSTRUCTED,      // Multiline structure reconstructed
    LLE_HIST_BUF_EVENT_CONTENT_MODIFIED,             // Buffer content modified during edit
    LLE_HIST_BUF_EVENT_SAVE_REQUESTED,               // Save requested by user
    LLE_HIST_BUF_EVENT_MULTILINE_DETECTED,           // Multiline content detected
    LLE_HIST_BUF_EVENT_STRUCTURE_ANALYZED            // Command structure analysis complete
} lle_history_buffer_event_type_t;

// Event data structures for different event types
typedef struct lle_history_edit_start_event {
    lle_history_entry_t *entry;                      // Entry being edited
    uint64_t session_id;                              // Edit session ID
    uint64_t start_timestamp;                         // Edit start time
} lle_history_edit_start_event_t;

typedef struct lle_buffer_loaded_event {
    lle_buffer_t *buffer;                             // Buffer that was loaded
    lle_history_entry_t *source_entry;               // Source history entry
    bool multiline_reconstructed;                     // Whether multiline was reconstructed
    size_t final_cursor_position;                     // Final cursor position
} lle_buffer_loaded_event_t;

typedef struct lle_structure_reconstructed_event {
    lle_command_structure_t *structure;              // Reconstructed structure
    const char *original_content;                     // Original content
    const char *reconstructed_content;                // Reconstructed content
    size_t reconstruction_time_us;                    // Time taken for reconstruction
} lle_structure_reconstructed_event_t;

// Event publishing for integration events
lle_result_t lle_history_buffer_publish_event(lle_history_buffer_integration_t *integration,
                                              lle_history_buffer_event_type_t event_type,
                                              void *event_data,
                                              size_t data_size) {
    if (!integration || !integration->system_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Create LLE event
    lle_event_t *event = NULL;
    lle_result_t result = lle_event_create(&event, 
                                          LLE_EVENT_HISTORY_BUFFER_INTEGRATION,
                                          integration->memory_pool);
    
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Set event data
    event->data = event_data;
    event->data_size = data_size;
    event->timestamp = lle_get_current_timestamp_ns();
    
    // Add integration-specific data
    lle_history_buffer_event_header_t *header = lle_memory_pool_alloc(
        integration->memory_pool, sizeof(lle_history_buffer_event_header_t));
    
    if (header) {
        header->integration_event_type = event_type;
        header->integration_system = integration;
        header->session_id = integration->session_counter;
        
        // Chain event data
        header->original_data = event->data;
        header->original_data_size = event->data_size;
        
        event->data = header;
        event->data_size = sizeof(lle_history_buffer_event_header_t);
    }
    
    // Publish through event system
    result = lle_event_system_publish(integration->event_system, event);
    
    return result;
}
```

---

## 8. Performance and Memory Management

### 8.1 Edit Session Caching

```c
// High-performance caching for edit sessions and reconstructed content
typedef struct lle_edit_cache {
    lle_hash_table_t *reconstruction_cache;          // Cache for reconstructed content
    lle_hash_table_t *structure_cache;               // Cache for analyzed structures
    lle_lru_cache_t *recent_edits_cache;             // LRU cache for recent edits
    
    // Cache statistics
    uint64_t reconstruction_hits;                     // Reconstruction cache hits
    uint64_t reconstruction_misses;                   // Reconstruction cache misses
    uint64_t structure_hits;                          // Structure cache hits  
    uint64_t structure_misses;                        // Structure cache misses
    
    // Memory management
    lle_memory_pool_t *cache_memory_pool;            // Dedicated cache memory pool
    size_t max_cache_size;                            // Maximum cache size
    size_t current_cache_size;                        // Current cache size
    
    // Performance tuning
    uint32_t reconstruction_ttl_seconds;             // Reconstruction TTL
    uint32_t structure_ttl_seconds;                   // Structure analysis TTL
    bool enable_aggressive_caching;                   // Aggressive caching flag
} lle_edit_cache_t;

// Cache-aware reconstruction with performance optimization
lle_result_t lle_cached_multiline_reconstruction(lle_history_buffer_integration_t *integration,
                                                 lle_history_entry_t *entry,
                                                 char **reconstructed_content,
                                                 size_t *content_length) {
    if (!integration || !entry || !reconstructed_content) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Step 1: Generate cache key
    uint64_t cache_key = lle_generate_reconstruction_cache_key(entry);
    
    // Step 2: Check reconstruction cache
    lle_cached_reconstruction_t *cached_result = lle_hash_table_get(
        integration->edit_cache->reconstruction_cache, &cache_key, sizeof(cache_key));
    
    if (cached_result && lle_is_cache_entry_valid(cached_result, 
                           integration->edit_cache->reconstruction_ttl_seconds)) {
        
        // Cache hit - return cached content
        *reconstructed_content = lle_string_duplicate(
            cached_result->content, integration->memory_pool);
        *content_length = cached_result->content_length;
        
        integration->edit_cache->reconstruction_hits++;
        
        return *reconstructed_content ? LLE_SUCCESS : LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Cache miss - perform reconstruction
    integration->edit_cache->reconstruction_misses++;
    
    lle_result_t result;
    if (entry->is_multiline && entry->structure_info) {
        // Use cached structure if available
        result = lle_reconstruction_engine_reconstruct_multiline(
            integration->reconstruction,
            entry->original_multiline ? entry->original_multiline : entry->command,
            entry->original_multiline ? entry->original_length : entry->command_length,
            entry->structure_info,
            reconstructed_content,
            content_length);
    } else {
        // Simple content duplication
        const char *source = entry->original_multiline ? entry->original_multiline : entry->command;
        size_t source_len = entry->original_multiline ? entry->original_length : entry->command_length;
        
        *reconstructed_content = lle_string_duplicate(source, integration->memory_pool);
        *content_length = source_len;
        result = *reconstructed_content ? LLE_SUCCESS : LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Step 3: Cache successful reconstruction
    if (result == LLE_SUCCESS && *reconstructed_content) {
        lle_cached_reconstruction_t *cache_entry = lle_memory_pool_alloc(
            integration->edit_cache->cache_memory_pool, sizeof(lle_cached_reconstruction_t));
        
        if (cache_entry) {
            cache_entry->content = lle_string_duplicate(
                *reconstructed_content, integration->edit_cache->cache_memory_pool);
            cache_entry->content_length = *content_length;
            cache_entry->cache_timestamp = lle_get_current_timestamp();
            cache_entry->access_count = 1;
            
            lle_hash_table_insert(integration->edit_cache->reconstruction_cache,
                                 &cache_key, sizeof(cache_key), cache_entry);
        }
    }
    
    return result;
}
```

---

## 9. Event System Integration

### 9.1 History Edit Events

```c
// Register history-buffer integration events with the core event system
lle_result_t lle_history_buffer_register_events(lle_history_buffer_integration_t *integration,
                                                lle_event_system_t *event_system) {
    if (!integration || !event_system) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    lle_result_t result = LLE_SUCCESS;
    
    // Register integration event types
    result = lle_event_system_register_type(event_system, LLE_EVENT_HISTORY_BUFFER_INTEGRATION);
    if (result != LLE_SUCCESS) return result;
    
    result = lle_event_system_register_type(event_system, LLE_EVENT_HISTORY_EDIT_START);
    if (result != LLE_SUCCESS) return result;
    
    result = lle_event_system_register_type(event_system, LLE_EVENT_HISTORY_EDIT_COMPLETE);
    if (result != LLE_SUCCESS) return result;
    
    result = lle_event_system_register_type(event_system, LLE_EVENT_HISTORY_EDIT_CANCEL);
    if (result != LLE_SUCCESS) return result;
    
    result = lle_event_system_register_type(event_system, LLE_EVENT_BUFFER_LOADED_FROM_HISTORY);
    if (result != LLE_SUCCESS) return result;
    
    // Register integration event handler
    lle_event_handler_t handler = {
        .handler_func = lle_history_buffer_event_handler,
        .user_data = integration,
        .priority = LLE_EVENT_PRIORITY_HIGH
    };
    
    result = lle_event_system_register_handler(event_system, 
                                              LLE_EVENT_HISTORY_BUFFER_INTEGRATION, 
                                              &handler);
    
    integration->event_system = event_system;
    
    return result;
}

// Event handler for history-buffer integration events
lle_result_t lle_history_buffer_event_handler(lle_event_t *event, void *user_data) {
    lle_history_buffer_integration_t *integration = 
        (lle_history_buffer_integration_t *)user_data;
    
    if (!event || !integration) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Extract integration-specific event data
    lle_history_buffer_event_header_t *header = 
        (lle_history_buffer_event_header_t *)event->data;
    
    if (!header || header->integration_system != integration) {
        return LLE_SUCCESS; // Not our event
    }
    
    lle_result_t result = LLE_SUCCESS;
    
    // Handle different integration event types
    switch (header->integration_event_type) {
        case LLE_HIST_BUF_EVENT_EDIT_START: {
            lle_history_edit_start_event_t *edit_data = 
                (lle_history_edit_start_event_t *)header->original_data;
            
            result = lle_handle_edit_start_event(integration, edit_data);
            break;
        }
        
        case LLE_HIST_BUF_EVENT_EDIT_COMPLETE: {
            lle_history_entry_t *completed_entry = 
                (lle_history_entry_t *)header->original_data;
            
            result = lle_handle_edit_complete_event(integration, completed_entry);
            break;
        }
        
        case LLE_HIST_BUF_EVENT_BUFFER_LOADED: {
            lle_buffer_loaded_event_t *buffer_data = 
                (lle_buffer_loaded_event_t *)header->original_data;
            
            result = lle_handle_buffer_loaded_event(integration, buffer_data);
            break;
        }
        
        case LLE_HIST_BUF_EVENT_STRUCTURE_RECONSTRUCTED: {
            lle_structure_reconstructed_event_t *structure_data = 
                (lle_structure_reconstructed_event_t *)header->original_data;
            
            result = lle_handle_structure_reconstructed_event(integration, structure_data);
            break;
        }
        
        default:
            result = LLE_SUCCESS; // Unknown event type, ignore
            break;
    }
    
    return result;
}
```

---

## 10. Error Handling and Recovery

### 10.1 Comprehensive Error Management

```c
// Error handling for history-buffer integration operations
typedef enum lle_history_buffer_error_type {
    LLE_HIST_BUF_ERROR_NONE = 0,
    LLE_HIST_BUF_ERROR_ENTRY_NOT_FOUND,
    LLE_HIST_BUF_ERROR_BUFFER_LOAD_FAILED,
    LLE_HIST_BUF_ERROR_RECONSTRUCTION_FAILED,
    LLE_HIST_BUF_ERROR_STRUCTURE_ANALYSIS_FAILED,
    LLE_HIST_BUF_ERROR_EDIT_SESSION_ACTIVE,
    LLE_HIST_BUF_ERROR_CALLBACK_FAILED,
    LLE_HIST_BUF_ERROR_PRESERVATION_FAILED,
    LLE_HIST_BUF_ERROR_INVALID_MULTILINE,
    LLE_HIST_BUF_ERROR_CACHE_CORRUPTION
} lle_history_buffer_error_type_t;

// Error recovery system
lle_result_t lle_history_buffer_handle_error(lle_history_buffer_integration_t *integration,
                                             lle_history_buffer_error_type_t error_type,
                                             const char *context_info,
                                             bool attempt_recovery) {
    if (!integration) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Log error
    lle_log_error("History-Buffer Integration Error: %s, Context: %s",
                  lle_history_buffer_error_to_string(error_type),
                  context_info ? context_info : "none");
    
    // Update error statistics
    integration->metrics->error_count++;
    integration->metrics->last_error_time = lle_get_current_timestamp();
    integration->metrics->last_error_type = error_type;
    
    lle_result_t recovery_result = LLE_SUCCESS;
    
    if (attempt_recovery) {
        switch (error_type) {
            case LLE_HIST_BUF_ERROR_BUFFER_LOAD_FAILED:
                // Attempt simple content loading
                recovery_result = lle_attempt_simple_buffer_load_recovery(integration);
                break;
                
            case LLE_HIST_BUF_ERROR_RECONSTRUCTION_FAILED:
                // Fall back to original content without reconstruction
                recovery_result = lle_attempt_reconstruction_fallback_recovery(integration);
                break;
                
            case LLE_HIST_BUF_ERROR_STRUCTURE_ANALYSIS_FAILED:
                // Treat as simple command without structure
                recovery_result = lle_attempt_structure_analysis_recovery(integration);
                break;
                
            case LLE_HIST_BUF_ERROR_EDIT_SESSION_ACTIVE:
                // Clean up active session and retry
                recovery_result = lle_attempt_session_cleanup_recovery(integration);
                break;
                
            case LLE_HIST_BUF_ERROR_CACHE_CORRUPTION:
                // Clear cache and rebuild
                recovery_result = lle_attempt_cache_rebuild_recovery(integration);
                break;
                
            default:
                recovery_result = LLE_ERROR_RECOVERY_NOT_POSSIBLE;
                break;
        }
        
        if (recovery_result == LLE_SUCCESS) {
            integration->metrics->successful_recoveries++;
        } else {
            integration->metrics->failed_recoveries++;
        }
    }
    
    return recovery_result;
}
```

---

## 11. Testing and Validation

### 11.1 Comprehensive Test Framework

```c
// Test framework for history-buffer integration
typedef struct lle_history_buffer_test_suite {
    // Core functionality tests
    bool (*test_interactive_history_editing)(void);
    bool (*test_multiline_recall_and_restoration)(void);
    bool (*test_buffer_loading_from_history)(void);
    bool (*test_original_multiline_preservation)(void);
    bool (*test_command_structure_reconstruction)(void);
    
    // Callback system tests
    bool (*test_edit_callbacks_invocation)(void);
    bool (*test_callback_error_handling)(void);
    bool (*test_session_lifecycle_management)(void);
    
    // Performance tests
    bool (*test_reconstruction_performance)(void);
    bool (*test_cache_efficiency)(void);
    bool (*test_memory_usage_optimization)(void);
    
    // Integration tests
    bool (*test_event_system_integration)(void);
    bool (*test_complex_multiline_constructs)(void);
    bool (*test_nested_structure_handling)(void);
    
    // Error handling tests
    bool (*test_error_recovery_mechanisms)(void);
    bool (*test_corrupted_data_handling)(void);
    bool (*test_memory_exhaustion_recovery)(void);
} lle_history_buffer_test_suite_t;

// Test multiline command reconstruction
bool test_multiline_for_loop_reconstruction(void) {
    // Test data: for loop with proper indentation
    const char *original_multiline = 
        "for file in *.txt; do\n"
        "  echo \"Processing: $file\"\n"
        "  wc -l \"$file\"\n"
        "done";
    
    // Create test history entry
    lle_history_entry_t *test_entry = create_test_history_entry(
        original_multiline, strlen(original_multiline), true);
    
    if (!test_entry) {
        return false;
    }
    
    // Create integration system
    lle_history_buffer_integration_t *integration = NULL;
    if (create_test_integration_system(&integration) != LLE_SUCCESS) {
        destroy_test_history_entry(test_entry);
        return false;
    }
    
    // Create test buffer
    lle_buffer_t *test_buffer = NULL;
    if (lle_buffer_create(&test_buffer, 1024) != LLE_SUCCESS) {
        destroy_test_integration_system(integration);
        destroy_test_history_entry(test_entry);
        return false;
    }
    
    // Test reconstruction
    lle_result_t result = lle_history_buffer_load_entry_with_reconstruction(
        integration, test_entry, test_buffer, NULL);
    
    bool test_passed = false;
    
    if (result == LLE_SUCCESS) {
        // Verify buffer content
        char *buffer_content = NULL;
        size_t content_length = 0;
        
        if (lle_buffer_get_complete_content(test_buffer, &buffer_content, &content_length) == LLE_SUCCESS) {
            // Verify multiline structure is preserved
            test_passed = (strstr(buffer_content, "for file in") != NULL) &&
                         (strstr(buffer_content, "  echo") != NULL) &&
                         (strstr(buffer_content, "  wc -l") != NULL) &&
                         (strstr(buffer_content, "done") != NULL) &&
                         (lle_buffer_get_line_count(test_buffer) == 4);
            
            free(buffer_content);
        }
    }
    
    // Cleanup
    lle_buffer_destroy(test_buffer);
    destroy_test_integration_system(integration);
    destroy_test_history_entry(test_entry);
    
    return test_passed;
}
```

---

## 12. Implementation Roadmap

### 12.1 Phase 1: Core Integration (Priority 1)

**Timeline**: 1-2 weeks  
**Dependencies**: Completed history and buffer management systems

**Deliverables:**
- Basic history-buffer integration system
- `lle_history_edit_entry()` function implementation
- Simple buffer loading from history entries
- Basic callback system framework

**Success Criteria:**
- Can load historical commands into buffer for editing
- Basic edit session management working
- Simple callbacks invoked correctly

### 12.2 Phase 2: Multiline Reconstruction (Priority 1)

**Timeline**: 2-3 weeks  
**Dependencies**: Phase 1 completion

**Deliverables:**
- Complete multiline structure analysis
- Intelligent command reconstruction engine
- `original_multiline` preservation system
- Advanced indentation and formatting

**Success Criteria:**
- Complex shell constructs (loops, conditionals) properly reconstructed
- Original multiline formatting preserved and restored
- Intelligent indentation applied during reconstruction

### 12.3 Phase 3: Advanced Features (Priority 2)

**Timeline**: 1-2 weeks  
**Dependencies**: Phase 2 completion

**Deliverables:**
- Performance optimization and caching
- Advanced error handling and recovery
- Comprehensive event system integration
- Complete testing framework

**Success Criteria:**
- Sub-millisecond reconstruction for common commands
- >75% cache hit rate for frequently accessed entries
- Zero memory leaks under continuous operation
- Comprehensive error recovery for all failure scenarios

### 12.4 Success Criteria

**Functional Requirements:**
- **Interactive History Editing**: Complete implementation of `lle_history_edit_entry()` with callback support
- **Multiline Preservation**: `original_multiline` field properly preserved and restored
- **Structure Reconstruction**: Complex shell constructs (loops, conditionals, functions) intelligently reconstructed
- **Buffer Integration**: Seamless loading of historical commands into editing buffer
- **Logical Unit Editing**: Commands treated as complete semantic units
- **Callback System**: Full callback-based editing framework operational

**Performance Requirements:**
- **Reconstruction Speed**: <0.5ms for simple commands, <2ms for complex multiline constructs  
- **Memory Efficiency**: <1MB baseline usage with efficient scaling
- **Cache Performance**: >75% hit rate for reconstruction cache
- **Event Processing**: <100us for history-buffer integration events
- **Session Management**: <1ms session creation and cleanup

**Quality Requirements:**
- **Memory Safety**: Zero leaks, proper cleanup of all resources
- **Error Handling**: Graceful degradation and recovery for all error scenarios
- **Thread Safety**: Concurrent access support with rwlocks
- **Integration Testing**: 100% pass rate on comprehensive test suite
- **Backwards Compatibility**: Full compatibility with existing Lush history

---

## Conclusion

**History-Buffer Integration Implementation Complete:** This specification provides the comprehensive implementation of the critical missing functionality from LLE_DESIGN_DOCUMENT.md - the seamless integration between history storage and buffer editing that enables interactive editing of multiline shell constructs.

**Key Implementation Benefits:**
- **Design Requirement Fulfillment**: Direct implementation of `lle_history_edit_entry()` and `original_multiline` preservation
- **Advanced Multiline Support**: Complete reconstruction of complex shell constructs with intelligent formatting
- **Performance Excellence**: Optimized caching and sub-millisecond operation targets
- **Enterprise Integration**: Seamless integration with existing Lush memory pool and event systems
- **Extensible Architecture**: Callback-based framework enabling unlimited customization

**Critical Functionality Delivered:**
1. **Interactive History Editing** - Complete callback-based editing system as designed
2. **Multiline Command Recall** - Full preservation and restoration of complex shell constructs  
3. **Structure-Aware Reconstruction** - Intelligent formatting with proper indentation
4. **Buffer-History Bridge** - Seamless integration between storage and editing
5. **Event System Integration** - Complete coordination with LLE core systems

**Implementation Status**: Complete specification ready for development  
**Next Priority**: Integration with existing buffer management and history system specifications to ensure seamless operation

**This specification directly addresses the critical gap identified in the AI assistant's failure analysis - providing the missing core functionality that makes the LLE design actually implementable as intended.**