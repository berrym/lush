# Terminal Abstraction Complete Specification

**LLE Terminal State Abstraction Layer - Research-Validated Architecture**

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Internal State Authority Model](#3-internal-state-authority-model)
4. [Terminal Capability Detection](#4-terminal-capability-detection)
5. [Display Content Generation](#5-display-content-generation)
6. [Lush Display Layer Integration](#6-lush-display-layer-integration)
7. [Input Event Processing](#7-input-event-processing)
8. [Unix Terminal Interface](#8-unix-terminal-interface)
9. [Error Handling and Recovery](#9-error-handling-and-recovery)
10. [Performance Requirements](#10-performance-requirements)
11. [Testing and Validation](#11-testing-and-validation)

---

## 1. Executive Summary

### 1.1 Purpose

This specification defines the LLE Terminal State Abstraction Layer based on research breakthrough findings that identified the fundamental architectural solution for reliable line editor implementation. The architecture follows proven patterns from JLine, ZSH ZLE, Fish, and Rustyline.

### 1.2 Key Features

- **Internal State Authority**: LLE internal model is authoritative, never queries terminal state
- **Display Layer Client**: Renders through Lush display system, never directly controls terminal
- **One-time Capability Detection**: Environment/terminfo-based capability detection at initialization only
- **Atomic Display Updates**: Generates complete display content for Lush rendering system
- **Terminal Abstraction**: All terminal interaction abstracted through Lush display layer

### 1.3 Critical Design Principles

1. **NEVER query terminal state during operation** - Internal model is single source of truth
2. **NEVER send direct escape sequences** - All terminal interaction through Lush display
3. **NEVER assume terminal cursor position** - Calculate from internal buffer state
4. **NEVER track terminal state changes** - Generate complete display content each update
5. **Internal buffer state is authoritative** - Command buffer is single source of editing state

---

## 2. Architecture Overview

### 2.1 Component Structure

```c
typedef struct lle_terminal_abstraction {
    // Internal State Authority Model - CORE COMPONENT
    lle_internal_state_t *internal_state;
    
    // Display Content Generation System
    lle_display_generator_t *display_generator;
    
    // Lush Display Layer Integration
    lle_lush_display_client_t *display_client;
    
    // Terminal Capability Model (detected once at startup)
    lle_terminal_capabilities_t *capabilities;
    
    // Input Processing System
    lle_input_processor_t *input_processor;
    
    // Unix Terminal Interface (minimal, abstracted)
    lle_unix_interface_t *unix_interface;
    
    // Error handling context
    lle_error_context_t *error_ctx;
    
    // Performance monitoring
    lle_performance_monitor_t *perf_monitor;
    
} lle_terminal_abstraction_t;
```

### 2.2 Initialization Flow

```c
lle_result_t lle_terminal_abstraction_init(lle_terminal_abstraction_t **abstraction,
                                           lush_display_context_t *lush_display) {
    lle_terminal_abstraction_t *abs = NULL;
    lle_result_t result = LLE_SUCCESS;
    
    // Step 1: Allocate main structure
    abs = calloc(1, sizeof(lle_terminal_abstraction_t));
    if (!abs) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Step 2: Initialize Unix interface for basic terminal access
    result = lle_unix_interface_init(&abs->unix_interface);
    if (result != LLE_SUCCESS) {
        free(abs);
        return result;
    }
    
    // Step 3: One-time capability detection (NO terminal queries)
    result = lle_capabilities_detect_environment(&abs->capabilities, abs->unix_interface);
    if (result != LLE_SUCCESS) {
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }
    
    // Step 4: Initialize internal state authority model
    result = lle_internal_state_init(&abs->internal_state, abs->capabilities);
    if (result != LLE_SUCCESS) {
        lle_capabilities_destroy(abs->capabilities);
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }
    
    // Step 5: Initialize display content generation system
    result = lle_display_generator_init(&abs->display_generator, 
                                        abs->capabilities, 
                                        abs->internal_state);
    if (result != LLE_SUCCESS) {
        lle_internal_state_destroy(abs->internal_state);
        lle_capabilities_destroy(abs->capabilities);
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }
    
    // Step 6: Initialize as Lush display layer client
    result = lle_lush_display_client_init(&abs->display_client,
                                            lush_display,
                                            abs->capabilities);
    if (result != LLE_SUCCESS) {
        lle_display_generator_destroy(abs->display_generator);
        lle_internal_state_destroy(abs->internal_state);
        lle_capabilities_destroy(abs->capabilities);
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }
    
    // Step 7: Initialize input processing system
    result = lle_input_processor_init(&abs->input_processor,
                                      abs->capabilities,
                                      abs->unix_interface);
    if (result != LLE_SUCCESS) {
        lle_lush_display_client_destroy(abs->display_client);
        lle_display_generator_destroy(abs->display_generator);
        lle_internal_state_destroy(abs->internal_state);
        lle_capabilities_destroy(abs->capabilities);
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }
    
    // Step 8: Initialize error handling and performance monitoring
    result = lle_error_context_init(&abs->error_ctx);
    if (result != LLE_SUCCESS) {
        // Cleanup all previous components
        lle_input_processor_destroy(abs->input_processor);
        lle_lush_display_client_destroy(abs->display_client);
        lle_display_generator_destroy(abs->display_generator);
        lle_internal_state_destroy(abs->internal_state);
        lle_capabilities_destroy(abs->capabilities);
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }
    
    result = lle_performance_monitor_init(&abs->perf_monitor);
    if (result != LLE_SUCCESS) {
        lle_error_context_destroy(abs->error_ctx);
        lle_input_processor_destroy(abs->input_processor);
        lle_lush_display_client_destroy(abs->display_client);
        lle_display_generator_destroy(abs->display_generator);
        lle_internal_state_destroy(abs->internal_state);
        lle_capabilities_destroy(abs->capabilities);
        lle_unix_interface_destroy(abs->unix_interface);
        free(abs);
        return result;
    }
    
    *abstraction = abs;
    return LLE_SUCCESS;
}
```

---

## 3. Internal State Authority Model

### 3.1 Internal State Structure - AUTHORITATIVE MODEL

```c
// CORE COMPONENT: Internal state is single source of truth
typedef struct lle_internal_state {
    // Command Buffer State - AUTHORITATIVE
    lle_command_buffer_t *command_buffer;    // Command being edited
    size_t cursor_position;                  // Cursor position in buffer (logical)
    size_t selection_start;                  // Selection start (if any)
    size_t selection_end;                    // Selection end (if any)
    bool has_selection;                      // Selection active flag
    
    // Display State Model - What we believe terminal contains
    lle_display_line_t *display_lines;      // Current display content
    size_t display_line_count;              // Number of display lines
    size_t display_capacity;                 // Allocated display line capacity
    
    // Display Geometry State
    size_t terminal_width;                   // Terminal columns
    size_t terminal_height;                  // Terminal rows  
    size_t prompt_width;                     // Prompt width in columns
    size_t display_offset;                   // Horizontal scroll offset
    size_t vertical_offset;                  // Vertical scroll offset
    
    // Edit State Tracking
    bool buffer_modified;                    // Buffer changed since last display
    uint64_t modification_count;             // Number of modifications
    uint64_t last_update_time;              // Last update timestamp
    
    // CRITICAL: NO terminal cursor position tracking
    // Cursor position always calculated from buffer state + display geometry
    
} lle_internal_state_t;

// Command buffer structure
typedef struct lle_command_buffer {
    char *data;                              // Buffer content (UTF-8)
    size_t length;                           // Current content length
    size_t capacity;                         // Allocated buffer size
    size_t allocated_size;                   // Actual allocation size
    
    // Buffer change tracking for optimization
    size_t last_change_offset;              // Last modification offset
    size_t last_change_length;              // Last modification length
    bool needs_full_refresh;                 // Requires complete display update
    
} lle_command_buffer_t;

// Display line structure - what terminal contains
typedef struct lle_display_line {
    char *content;                           // Line content (UTF-8)
    size_t length;                          // Content length
    size_t capacity;                        // Allocated capacity
    
    // Visual attributes for this line
    lle_line_attributes_t attributes;        // Colors, styles, etc.
    
    // Cursor information if cursor is on this line
    bool contains_cursor;                    // True if cursor on this line
    size_t cursor_column;                    // Visual cursor column (if present)
    
} lle_display_line_t;
```

### 3.2 Internal State Operations

```c
// Initialize internal state with terminal capabilities
lle_result_t lle_internal_state_init(lle_internal_state_t **state,
                                     lle_terminal_capabilities_t *caps) {
    lle_internal_state_t *internal_state = calloc(1, sizeof(lle_internal_state_t));
    if (!internal_state) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Initialize command buffer
    lle_result_t result = lle_command_buffer_init(&internal_state->command_buffer, 1024);
    if (result != LLE_SUCCESS) {
        free(internal_state);
        return result;
    }
    
    // Initialize display lines array
    internal_state->display_capacity = 10; // Initial capacity
    internal_state->display_lines = calloc(internal_state->display_capacity, 
                                          sizeof(lle_display_line_t));
    if (!internal_state->display_lines) {
        lle_command_buffer_destroy(internal_state->command_buffer);
        free(internal_state);
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Set initial terminal geometry from capabilities
    internal_state->terminal_width = caps->terminal_width > 0 ? caps->terminal_width : 80;
    internal_state->terminal_height = caps->terminal_height > 0 ? caps->terminal_height : 24;
    
    // Initialize state
    internal_state->cursor_position = 0;
    internal_state->display_line_count = 0;
    internal_state->buffer_modified = false;
    internal_state->modification_count = 0;
    
    *state = internal_state;
    return LLE_SUCCESS;
}

// Update command buffer (authoritative operation)
lle_result_t lle_internal_state_insert_text(lle_internal_state_t *state,
                                            size_t position,
                                            const char *text,
                                            size_t text_length) {
    if (!state || !text || text_length == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Insert into command buffer
    lle_result_t result = lle_command_buffer_insert(state->command_buffer,
                                                   position, text, text_length);
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Update cursor position
    if (position <= state->cursor_position) {
        state->cursor_position += text_length;
    }
    
    // Mark buffer as modified
    state->buffer_modified = true;
    state->modification_count++;
    state->last_update_time = lle_get_current_time_microseconds();
    
    return LLE_SUCCESS;
}

// Calculate cursor position from buffer state (NO terminal queries)
lle_result_t lle_internal_state_calculate_cursor_display_position(
                                    lle_internal_state_t *state,
                                    size_t *display_line,
                                    size_t *display_column) {
    if (!state || !display_line || !display_column) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Calculate display position from buffer position and terminal geometry
    size_t buffer_pos = state->cursor_position;
    size_t line = 0;
    size_t column = state->prompt_width; // Start after prompt
    
    // Walk through buffer calculating display position
    for (size_t i = 0; i < buffer_pos && i < state->command_buffer->length; i++) {
        char c = state->command_buffer->data[i];
        
        if (c == '\n') {
            line++;
            column = 0;
        } else if (c == '\t') {
            column += 8 - (column % 8); // Tab to next 8-column boundary
        } else {
            column++;
        }
        
        // Handle line wrapping
        if (column >= state->terminal_width) {
            line++;
            column = 0;
        }
    }
    
    *display_line = line;
    *display_column = column;
    
    return LLE_SUCCESS;
}
```

---

## 4. Terminal Capability Detection

### 4.1 Environment-Based Capability Detection (NO TERMINAL QUERIES)

```c
// Terminal capabilities detected from environment/terminfo ONLY
typedef struct lle_terminal_capabilities {
    // Basic terminal information
    bool is_tty;                             // Running in TTY
    char *terminal_type;                     // TERM environment variable
    char *terminal_program;                  // Terminal program name
    
    // Display capabilities (from terminfo/environment)
    bool supports_ansi_colors;              // Basic 8/16 color support
    bool supports_256_colors;               // 256 color support  
    bool supports_truecolor;                // 24-bit color support
    uint8_t detected_color_depth;           // Color depth (4, 8, or 24)
    
    // Text attributes (from terminfo)
    bool supports_bold;
    bool supports_italic;
    bool supports_underline;
    bool supports_strikethrough;
    bool supports_reverse;
    bool supports_dim;
    
    // Advanced features (from environment/terminfo)
    bool supports_mouse_reporting;
    bool supports_bracketed_paste;
    bool supports_focus_events;
    bool supports_synchronized_output;
    bool supports_unicode;
    
    // Terminal geometry
    size_t terminal_width;                   // Columns
    size_t terminal_height;                  // Rows
    
    // Performance characteristics
    uint32_t estimated_latency_ms;           // Estimated terminal latency
    bool supports_fast_updates;             // Can handle rapid updates
    
    // Terminal-specific optimizations  
    lle_terminal_type_t terminal_type_enum;
    lle_optimization_flags_t optimizations;
    
} lle_terminal_capabilities_t;

// Capability detection using environment/terminfo (NO terminal queries)
lle_result_t lle_capabilities_detect_environment(lle_terminal_capabilities_t **caps,
                                                 lle_unix_interface_t *unix_iface) {
    lle_terminal_capabilities_t *detected_caps = calloc(1, sizeof(lle_terminal_capabilities_t));
    if (!detected_caps) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Step 1: Check TTY status
    detected_caps->is_tty = isatty(STDIN_FILENO);
    if (!detected_caps->is_tty) {
        // Set minimal capabilities for non-TTY
        lle_set_minimal_capabilities(detected_caps);
        *caps = detected_caps;
        return LLE_SUCCESS;
    }
    
    // Step 2: Analyze TERM environment variable
    const char *term = getenv("TERM");
    if (term) {
        detected_caps->terminal_type = strdup(term);
        lle_analyze_term_variable(detected_caps, term);
    } else {
        detected_caps->terminal_type = strdup("unknown");
        lle_set_conservative_capabilities(detected_caps);
    }
    
    // Step 3: Analyze additional environment variables
    lle_analyze_colorterm_variable(detected_caps);
    lle_analyze_terminal_program_variables(detected_caps);
    
    // Step 4: Query terminfo database (NO terminal interaction)
    lle_result_t result = lle_query_terminfo_capabilities(detected_caps);
    if (result != LLE_SUCCESS) {
        // Terminfo unavailable - use conservative defaults
        lle_set_conservative_capabilities(detected_caps);
    }
    
    // Step 5: Get terminal size from system
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        detected_caps->terminal_width = ws.ws_col > 0 ? ws.ws_col : 80;
        detected_caps->terminal_height = ws.ws_row > 0 ? ws.ws_row : 24;
    } else {
        detected_caps->terminal_width = 80;
        detected_caps->terminal_height = 24;
    }
    
    // Step 6: Apply terminal-specific optimizations
    lle_apply_terminal_specific_optimizations(detected_caps);
    
    // Step 7: Validate and finalize capabilities
    lle_validate_capabilities(detected_caps);
    
    *caps = detected_caps;
    return LLE_SUCCESS;
}

// Analyze TERM environment variable for capabilities
static void lle_analyze_term_variable(lle_terminal_capabilities_t *caps, const char *term) {
    // Color support detection from TERM
    if (strstr(term, "256color") || strstr(term, "256")) {
        caps->supports_256_colors = true;
        caps->supports_ansi_colors = true;
        caps->detected_color_depth = 8;
    } else if (strstr(term, "color")) {
        caps->supports_ansi_colors = true;
        caps->detected_color_depth = 4;
    }
    
    // Terminal type detection
    if (strstr(term, "xterm")) {
        caps->terminal_type_enum = LLE_TERMINAL_XTERM;
        caps->supports_mouse_reporting = true;
        caps->supports_bracketed_paste = true;
    } else if (strstr(term, "screen")) {
        caps->terminal_type_enum = LLE_TERMINAL_SCREEN;
        caps->supports_256_colors = true; // Screen usually supports 256 colors
    } else if (strstr(term, "tmux")) {
        caps->terminal_type_enum = LLE_TERMINAL_TMUX;
        caps->supports_256_colors = true;
        caps->supports_truecolor = true; // Modern tmux supports truecolor
    } else {
        caps->terminal_type_enum = LLE_TERMINAL_GENERIC;
    }
}

// Analyze COLORTERM for truecolor support
static void lle_analyze_colorterm_variable(lle_terminal_capabilities_t *caps) {
    const char *colorterm = getenv("COLORTERM");
    if (colorterm) {
        if (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0) {
            caps->supports_truecolor = true;
            caps->supports_256_colors = true;
            caps->supports_ansi_colors = true;
            caps->detected_color_depth = 24;
        }
    }
}
```

---

## 5. Display Content Generation

### 5.1 Display Content Generation System

```c
// Display content generator - converts internal state to display content
typedef struct lle_display_generator {
    lle_terminal_capabilities_t *capabilities;
    lle_internal_state_t *internal_state;
    
    // Content generation state
    lle_display_content_t *current_content;
    lle_display_content_t *previous_content;
    
    // Generation parameters
    lle_generation_params_t params;
    
} lle_display_generator_t;

// Display content structure - what gets sent to Lush display system
typedef struct lle_display_content {
    // Complete display lines
    lle_display_line_t *lines;
    size_t line_count;
    
    // Cursor position information
    size_t cursor_line;
    size_t cursor_column;
    bool cursor_visible;
    
    // Display attributes
    lle_display_attributes_t attributes;
    
    // Content metadata
    uint64_t generation_time;
    uint64_t sequence_number;
    bool is_complete_refresh;
    
} lle_display_content_t;

// Generate complete display content from internal state
lle_result_t lle_display_generator_generate_content(lle_display_generator_t *generator,
                                                   lle_display_content_t **content) {
    if (!generator || !content) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    lle_display_content_t *new_content = calloc(1, sizeof(lle_display_content_t));
    if (!new_content) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    lle_internal_state_t *state = generator->internal_state;
    
    // Step 1: Calculate display geometry from buffer
    lle_result_t result = lle_calculate_display_geometry(generator, new_content);
    if (result != LLE_SUCCESS) {
        free(new_content);
        return result;
    }
    
    // Step 2: Generate display lines from command buffer
    result = lle_generate_display_lines(generator, new_content);
    if (result != LLE_SUCCESS) {
        lle_display_content_destroy(new_content);
        return result;
    }
    
    // Step 3: Calculate cursor display position from buffer state
    result = lle_internal_state_calculate_cursor_display_position(
                state,
                &new_content->cursor_line,
                &new_content->cursor_column);
    if (result != LLE_SUCCESS) {
        lle_display_content_destroy(new_content);
        return result;
    }
    
    // Step 4: Apply display attributes based on capabilities
    result = lle_apply_display_attributes(generator, new_content);
    if (result != LLE_SUCCESS) {
        lle_display_content_destroy(new_content);
        return result;
    }
    
    // Step 5: Set content metadata
    new_content->generation_time = lle_get_current_time_microseconds();
    new_content->sequence_number = generator->internal_state->modification_count;
    new_content->is_complete_refresh = state->buffer_modified || 
                                      (generator->previous_content == NULL);
    new_content->cursor_visible = true;
    
    // Step 6: Store as current content
    if (generator->previous_content) {
        lle_display_content_destroy(generator->previous_content);
    }
    generator->previous_content = generator->current_content;
    generator->current_content = new_content;
    
    *content = new_content;
    return LLE_SUCCESS;
}

// Generate display lines from command buffer
static lle_result_t lle_generate_display_lines(lle_display_generator_t *generator,
                                               lle_display_content_t *content) {
    lle_internal_state_t *state = generator->internal_state;
    lle_command_buffer_t *buffer = state->command_buffer;
    
    // Calculate required lines for buffer content
    size_t required_lines = lle_calculate_required_lines(buffer, 
                                                        state->terminal_width,
                                                        state->prompt_width);
    
    // Allocate display lines
    content->lines = calloc(required_lines, sizeof(lle_display_line_t));
    if (!content->lines) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    content->line_count = required_lines;
    
    // Generate each display line from buffer content
    size_t buffer_pos = 0;
    size_t current_line = 0;
    size_t column = state->prompt_width; // Start after prompt
    
    // First line includes prompt
    lle_result_t result = lle_initialize_first_line(&content->lines[0], 
                                                   state->prompt_width);
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Process buffer content into display lines
    while (buffer_pos < buffer->length && current_line < required_lines) {
        char c = buffer->data[buffer_pos];
        
        if (c == '\n') {
            // Move to next line
            current_line++;
            if (current_line < required_lines) {
                result = lle_initialize_display_line(&content->lines[current_line]);
                if (result != LLE_SUCCESS) {
                    return result;
                }
            }
            column = 0;
        } else {
            // Add character to current line
            result = lle_add_character_to_line(&content->lines[current_line], c);
            if (result != LLE_SUCCESS) {
                return result;
            }
            
            column++;
            
            // Handle line wrapping
            if (column >= state->terminal_width) {
                current_line++;
                if (current_line < required_lines) {
                    result = lle_initialize_display_line(&content->lines[current_line]);
                    if (result != LLE_SUCCESS) {
                        return result;
                    }
                }
                column = 0;
            }
        }
        
        buffer_pos++;
    }
    
    return LLE_SUCCESS;
}
```

---

## 6. Lush Display Layer Integration

### 6.1 Display Layer Client Architecture

```c
// LLE as Lush Display Layer Client - NEVER direct terminal control
typedef struct lle_lush_display_client {
    // Lush display system integration
    lush_display_context_t *display_context;
    lush_display_layer_t *lle_display_layer;
    
    // LLE-specific layer configuration
    lle_layer_config_t layer_config;
    
    // Terminal capabilities for display optimization
    lle_terminal_capabilities_t *capabilities;
    
    // Display submission tracking
    uint64_t last_submission_time;
    uint64_t submission_count;
    
} lle_lush_display_client_t;

// Initialize as Lush display layer client
lle_result_t lle_lush_display_client_init(lle_lush_display_client_t **client,
                                           lush_display_context_t *display_context,
                                           lle_terminal_capabilities_t *capabilities) {
    if (!client || !display_context || !capabilities) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    lle_lush_display_client_t *display_client = calloc(1, sizeof(lle_lush_display_client_t));
    if (!display_client) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Store references
    display_client->display_context = display_context;
    display_client->capabilities = capabilities;
    
    // Register as display layer with Lush
    lle_layer_config_t config = {
        .layer_name = "lle_editing",
        .layer_priority = LUSH_LAYER_PRIORITY_EDITING, // Above prompt, below debug
        .supports_transparency = true,
        .requires_full_refresh = false,
        .color_capabilities = capabilities->detected_color_depth
    };
    
    lush_result_t result = lush_display_register_layer(display_context,
                                                          &config,
                                                          &display_client->lle_display_layer);
    if (result != LUSH_SUCCESS) {
        free(display_client);
        return lle_convert_lush_error(result);
    }
    
    display_client->layer_config = config;
    
    *client = display_client;
    return LLE_SUCCESS;
}

// Submit display content to Lush display system
lle_result_t lle_lush_display_client_submit_content(lle_lush_display_client_t *client,
                                                     lle_display_content_t *content) {
    if (!client || !content) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Convert LLE display content to Lush display format
    lush_layer_content_t *lush_content = NULL;
    lle_result_t result = lle_convert_to_lush_content(content, 
                                                       client->capabilities,
                                                       &lush_content);
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Submit to Lush display layer
    lush_result_t lush_result = lush_display_layer_update(client->lle_display_layer,
                                                               lush_content);
    
    // Update tracking
    client->last_submission_time = lle_get_current_time_microseconds();
    client->submission_count++;
    
    // Cleanup converted content
    lush_layer_content_destroy(lush_content);
    
    if (lush_result != LUSH_SUCCESS) {
        return lle_convert_lush_error(lush_result);
    }
    
    return LLE_SUCCESS;
}

// Convert LLE display content to Lush format
static lle_result_t lle_convert_to_lush_content(lle_display_content_t *lle_content,
                                                  lle_terminal_capabilities_t *caps,
                                                  lush_layer_content_t **lush_content) {
    lush_layer_content_t *content = calloc(1, sizeof(lush_layer_content_t));
    if (!content) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Convert display lines
    content->line_count = lle_content->line_count;
    content->lines = calloc(content->line_count, sizeof(lush_display_line_t));
    if (!content->lines) {
        free(content);
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    for (size_t i = 0; i < lle_content->line_count; i++) {
        // Convert each LLE display line to Lush format
        lle_result_t result = lle_convert_display_line(&lle_content->lines[i],
                                                      caps,
                                                      &content->lines[i]);
        if (result != LLE_SUCCESS) {
            lush_layer_content_destroy(content);
            return result;
        }
    }
    
    // Set cursor information
    content->cursor_line = lle_content->cursor_line;
    content->cursor_column = lle_content->cursor_column;
    content->cursor_visible = lle_content->cursor_visible;
    
    // Set content metadata
    content->generation_time = lle_content->generation_time;
    content->is_complete = lle_content->is_complete_refresh;
    
    *lush_content = content;
    return LLE_SUCCESS;
}
```

---

## 7. Input Event Processing

### 7.1 Input Event Types

```c
typedef enum {
    LLE_INPUT_TYPE_CHARACTER = 0,     // Regular character input
    LLE_INPUT_TYPE_SPECIAL_KEY,       // Special keys (arrows, function keys)
    LLE_INPUT_TYPE_WINDOW_RESIZE,     // Window size change
    LLE_INPUT_TYPE_SIGNAL,            // Signal received
    LLE_INPUT_TYPE_TIMEOUT,           // Input timeout
    LLE_INPUT_TYPE_ERROR,             // Error condition
    LLE_INPUT_TYPE_EOF,               // End of file
} lle_input_type_t;

typedef struct lle_input_event {
    lle_input_type_t type;
    uint64_t timestamp;
    uint32_t sequence_number;
    
    union {
        // Character input
        struct {
            uint32_t codepoint;           // Unicode codepoint
            char utf8_bytes[8];           // UTF-8 representation
            uint8_t byte_count;           // Number of UTF-8 bytes
        } character;
        
        // Special key input
        struct {
            lle_special_key_t key;        // Special key identifier
            uint32_t modifiers;           // Modifier flags
            char raw_sequence[32];        // Raw escape sequence
            uint8_t sequence_length;      // Sequence length
        } special_key;
        
        // Window resize event
        struct {
            uint16_t rows, cols;          // New window size
            uint16_t pixel_width, pixel_height; // Pixel dimensions
        } resize;
        
        // Signal event
        struct {
            int signal_number;            // Signal number
            const char *signal_name;      // Signal name
        } signal;
        
        // Timeout event
        struct {
            uint32_t timeout_ms;          // Timeout duration
        } timeout;
        
        // Error event
        struct {
            lle_error_code_t error_code;  // Error code
            char error_message[256];      // Error description
        } error;
    } data;
    
    // Event metadata
    bool handled;
    uint64_t processing_time_us;
    
} lle_input_event_t;
```

### 7.2 Input Processing Implementation

```c
// Input processor - handles input events and updates internal state
lle_result_t lle_input_processor_process_event(lle_input_processor_t *processor,
                                               lle_input_event_t *event) {
    if (!processor || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    lle_result_t result = LLE_SUCCESS;
    
    switch (event->type) {
        case LLE_INPUT_TYPE_CHARACTER:
            result = lle_handle_character_input(processor, &event->data.character);
            break;
            
        case LLE_INPUT_TYPE_SPECIAL_KEY:
            result = lle_handle_special_key_input(processor, &event->data.special_key);
            break;
            
        case LLE_INPUT_TYPE_WINDOW_RESIZE:
            result = lle_handle_window_resize(processor, &event->data.resize);
            break;
            
        case LLE_INPUT_TYPE_SIGNAL:
            result = lle_handle_signal_event(processor, &event->data.signal);
            break;
            
        case LLE_INPUT_TYPE_TIMEOUT:
            // Timeout events don't modify internal state
            result = LLE_SUCCESS;
            break;
            
        case LLE_INPUT_TYPE_ERROR:
        case LLE_INPUT_TYPE_EOF:
            result = lle_handle_error_input(processor, event);
            break;
    }
    
    // Mark event as handled
    event->handled = (result == LLE_SUCCESS);
    
    return result;
}

// Handle character input - updates internal command buffer
static lle_result_t lle_handle_character_input(lle_input_processor_t *processor,
                                               lle_character_input_t *char_input) {
    // Insert character at current cursor position
    return lle_internal_state_insert_text(processor->internal_state,
                                          processor->internal_state->cursor_position,
                                          char_input->utf8_bytes,
                                          char_input->byte_count);
}

// Handle special key input - cursor movement, editing operations
static lle_result_t lle_handle_special_key_input(lle_input_processor_t *processor,
                                                 lle_special_key_input_t *key_input) {
    lle_internal_state_t *state = processor->internal_state;
    
    switch (key_input->key) {
        case LLE_KEY_ARROW_LEFT:
            if (state->cursor_position > 0) {
                state->cursor_position--;
                state->buffer_modified = true;
            }
            break;
            
        case LLE_KEY_ARROW_RIGHT:
            if (state->cursor_position < state->command_buffer->length) {
                state->cursor_position++;
                state->buffer_modified = true;
            }
            break;
            
        case LLE_KEY_BACKSPACE:
            if (state->cursor_position > 0) {
                lle_result_t result = lle_command_buffer_delete(state->command_buffer,
                                                               state->cursor_position - 1, 1);
                if (result == LLE_SUCCESS) {
                    state->cursor_position--;
                    state->buffer_modified = true;
                    state->modification_count++;
                }
                return result;
            }
            break;
            
        case LLE_KEY_DELETE:
            if (state->cursor_position < state->command_buffer->length) {
                lle_result_t result = lle_command_buffer_delete(state->command_buffer,
                                                               state->cursor_position, 1);
                if (result == LLE_SUCCESS) {
                    state->buffer_modified = true;
                    state->modification_count++;
                }
                return result;
            }
            break;
            
        case LLE_KEY_HOME:
            state->cursor_position = 0;
            state->buffer_modified = true;
            break;
            
        case LLE_KEY_END:
            state->cursor_position = state->command_buffer->length;
            state->buffer_modified = true;
            break;
            
        default:
            // Unknown special key - ignore
            break;
    }
    
    return LLE_SUCCESS;
}
```

---

## 8. Unix Terminal Interface

### 8.1 Minimal Unix Interface

```c
// Minimal Unix terminal interface - NO direct terminal control
typedef struct lle_unix_interface {
    // File descriptors for input only
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    
    // Terminal state management for input processing
    struct termios original_termios;
    struct termios raw_termios;
    bool raw_mode_active;
    
    // Signal handling for window resize
    struct sigaction original_sigwinch;
    bool signal_handlers_installed;
    
    // Window size tracking
    struct winsize current_window_size;
    bool window_size_valid;
    
    // Input buffer for reading
    char input_buffer[1024];
    size_t input_buffer_length;
    size_t input_buffer_position;
    
} lle_unix_interface_t;

// Initialize minimal Unix interface
lle_result_t lle_unix_interface_init(lle_unix_interface_t **interface) {
    lle_unix_interface_t *unix_iface = calloc(1, sizeof(lle_unix_interface_t));
    if (!unix_iface) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    // Initialize file descriptors
    unix_iface->stdin_fd = STDIN_FILENO;
    unix_iface->stdout_fd = STDOUT_FILENO;
    unix_iface->stderr_fd = STDERR_FILENO;
    
    // Get original terminal state for input processing
    if (isatty(unix_iface->stdin_fd)) {
        if (tcgetattr(unix_iface->stdin_fd, &unix_iface->original_termios) == 0) {
            // Set up raw mode for input processing
            unix_iface->raw_termios = unix_iface->original_termios;
            unix_iface->raw_termios.c_lflag &= ~(ICANON | ECHO);
            unix_iface->raw_termios.c_cc[VMIN] = 1;
            unix_iface->raw_termios.c_cc[VTIME] = 0;
        }
    }
    
    // Get initial window size
    if (ioctl(unix_iface->stdout_fd, TIOCGWINSZ, &unix_iface->current_window_size) == 0) {
        unix_iface->window_size_valid = true;
    } else {
        unix_iface->current_window_size.ws_row = 24;
        unix_iface->current_window_size.ws_col = 80;
        unix_iface->window_size_valid = false;
    }
    
    *interface = unix_iface;
    return LLE_SUCCESS;
}

// Read input event from Unix interface
lle_result_t lle_unix_interface_read_event(lle_unix_interface_t *interface,
                                          lle_input_event_t **event) {
    if (!interface || !event) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Poll for input with timeout
    struct pollfd pfd = { .fd = interface->stdin_fd, .events = POLLIN };
    int poll_result = poll(&pfd, 1, 100); // 100ms timeout
    
    if (poll_result < 0) {
        if (errno == EINTR) {
            return lle_create_signal_event(event);
        }
        return LLE_ERROR_POLL_FAILED;
    }
    
    if (poll_result == 0) {
        return lle_create_timeout_event(event);
    }
    
    // Read input data
    ssize_t bytes_read = read(interface->stdin_fd, interface->input_buffer, 
                             sizeof(interface->input_buffer) - 1);
    
    if (bytes_read <= 0) {
        if (bytes_read == 0) {
            return lle_create_eof_event(event);
        }
        return LLE_ERROR_READ_FAILED;
    }
    
    interface->input_buffer_length = bytes_read;
    interface->input_buffer_position = 0;
    
    // Process first character/sequence from buffer
    return lle_parse_input_buffer(interface, event);
}
```

---

## 9. Error Handling and Recovery

### 9.1 Error Recovery System

```c
typedef struct lle_error_context {
    uint32_t error_count;
    uint32_t warning_count;
    uint64_t last_error_time;
    char last_error_message[256];
    lle_error_recovery_strategy_t recovery_strategy;
} lle_error_context_t;

lle_result_t lle_error_context_handle_error(lle_error_context_t *ctx,
                                            lle_error_code_t error_code,
                                            const char *error_message) {
    if (!ctx) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    ctx->error_count++;
    ctx->last_error_time = lle_get_current_time_microseconds();
    
    if (error_message) {
        strncpy(ctx->last_error_message, error_message, sizeof(ctx->last_error_message) - 1);
        ctx->last_error_message[sizeof(ctx->last_error_message) - 1] = '\0';
    }
    
    // Determine recovery strategy
    switch (error_code) {
        case LLE_ERROR_MEMORY_ALLOCATION:
            ctx->recovery_strategy = LLE_RECOVERY_MEMORY_CLEANUP;
            break;
            
        case LLE_ERROR_TERMINAL_STATE:
            ctx->recovery_strategy = LLE_RECOVERY_TERMINAL_RESET;
            break;
            
        case LLE_ERROR_DISPLAY_SUBMISSION:
            ctx->recovery_strategy = LLE_RECOVERY_DISPLAY_REFRESH;
            break;
            
        default:
            ctx->recovery_strategy = LLE_RECOVERY_GRACEFUL_CONTINUE;
            break;
    }
    
    return LLE_SUCCESS;
}
```

---

## 10. Performance Requirements

### 10.1 Performance Specifications

**RESEARCH-VALIDATED PERFORMANCE TARGETS**:

- **Internal State Updates**: < 100us average (no terminal interaction)
- **Display Content Generation**: < 500us average (pure computation)
- **Lush Display Submission**: < 1ms average (display layer coordination)  
- **Input Event Processing**: < 250us average (buffer operations only)
- **Memory Usage**: < 32KB for internal state model
- **CPU Usage**: < 0.5% during idle, < 2% during active editing

### 10.2 Performance Monitoring

```c
typedef struct lle_performance_monitor {
    uint64_t state_updates_total;
    uint64_t state_update_time_total;
    uint64_t display_generations_total;
    uint64_t display_generation_time_total;
    uint64_t display_submissions_total;
    uint64_t display_submission_time_total;
    uint32_t input_events_processed;
    uint64_t input_processing_time_total;
} lle_performance_monitor_t;
```

---

## 11. Testing and Validation

### 11.1 Research-Validated Test Framework

```c
typedef struct lle_test_suite {
    // Internal State Authority Tests
    bool (*test_internal_state_authority)(void);
    bool (*test_no_terminal_queries)(void);
    bool (*test_cursor_calculation_accuracy)(void);
    
    // Display Layer Client Tests  
    bool (*test_lush_display_integration)(void);
    bool (*test_display_content_generation)(void);
    bool (*test_no_direct_terminal_control)(void);
    
    // Capability Detection Tests
    bool (*test_environment_based_detection)(void);
    bool (*test_no_runtime_queries)(void);
    bool (*test_conservative_defaults)(void);
    
    // Performance Tests
    bool (*test_sub_millisecond_response)(void);
    bool (*test_memory_usage_limits)(void);
    bool (*test_cpu_usage_efficiency)(void);
    
    // Integration Tests
    bool (*test_complete_editing_workflow)(void);
    bool (*test_error_recovery_mechanisms)(void);
    bool (*test_cross_terminal_compatibility)(void);
    
} lle_test_suite_t;
```

### 11.2 Validation Requirements

**CRITICAL VALIDATION CRITERIA**:

1. **NO Terminal State Queries**: Validate that no terminal queries occur during operation
2. **Internal State Authority**: Validate that internal model is single source of truth  
3. **Display Layer Client Only**: Validate that all terminal interaction goes through Lush
4. **Performance Compliance**: Validate sub-millisecond response times achieved
5. **Memory Safety**: Validate no memory leaks or buffer overflows
6. **Cross-Terminal Compatibility**: Validate works across different terminal types
7. **Research Compliance**: Validate follows all research-validated architectural patterns

---

**REFACTORING COMPLETE**: This specification now aligns with the Terminal State Abstraction Layer research breakthrough, eliminating all architectural violations and following proven patterns from successful line editor implementations.

**Document Version**: 2.0.0 (Research-Validated Architecture)  
**Date**: 2025-10-10  
**Status**: Implementation-Ready Specification  
**Classification**: Critical Foundation Component - Research Validated