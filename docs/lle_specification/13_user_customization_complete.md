# LLE USER CUSTOMIZATION SYSTEM COMPLETE SPECIFICATION
**Document 13 of 21 - Lush Line Editor (LLE) Epic Specification Project**

---

**Document Version**: 2.0.0  
**Specification Status**: Integration-Ready Specification (Phase 2 Integration Refactoring)
**Last Updated**: 2025-10-11  
**Integration Target**: Lush Shell v1.3.0+ LLE Integration  
**Dependencies**: Documents 01-12 (All Core Systems) + Integration Systems (22-26)

---

## **EXECUTIVE SUMMARY**

This specification defines a comprehensive user customization system enabling unlimited personalization of the Lush Line Editor through user-programmable editing operations, flexible key binding management, integrated scripting capabilities, and enterprise-grade configuration management. The system provides complete control over editor behavior while maintaining security, performance, and seamless integration with all LLE core systems.

**Key Capabilities**:
- **Complete Key Binding Control**: User-defined key mappings for any editing operation with GNU Readline compatibility
- **Keybinding Integration**: Seamless integration with default keybindings system for Emacs/Vi mode switching
- **Widget Hook Customization**: Complete integration with advanced prompt widget hooks for bottom-prompt and lifecycle management
- **Completion Menu Customization**: Full customization of interactive completion menu categories, ranking, and behavior
- **Programmable Edit Operations**: Custom user-defined editing commands and widgets  
- **Script Integration**: Lua and Python scripting support for advanced customization
- **Configuration Management**: Type-safe, schema-validated configuration system
- **Plugin Integration**: Seamless integration with LLE extensibility framework
- **Real-time Updates**: Dynamic customization without restart requirements
- **Enterprise Security**: Comprehensive sandboxing and permission management

**Performance Targets**:
- Sub-500us customization lookup and execution
- Zero-allocation customization processing during runtime
- <1ms script execution for user-defined operations
- >90% cache hit rate for customization data

---

## **HOW TO USE THE CUSTOMIZATION SYSTEM**

This section explains how users take advantage of the LLE User Customization System through practical examples and common use cases.

### **Getting Started with Customization**

#### **Basic Keybinding Customization**
Create custom keybindings for any LLE operation:

```bash
# Set custom keybindings through display command
display lle keybindings mode emacs          # Set to Emacs mode
display lle config set keybindings.ctrl-j "history-search-backward"
display lle config set keybindings.ctrl-k "history-search-forward"

# Save your customizations
config save
```

#### **Theme and Visual Customization**
Customize the appearance of LLE through theme commands:

```bash
# Customize LLE colors and appearance
theme lle colors set vibrant                # Use vibrant color scheme
theme lle syntax customize                  # Interactive syntax color setup
theme lle autosuggestions style            # Configure suggestion appearance
theme lle completion style                 # Configure completion menu styling

# Save theme customizations
config save
```

#### **Plugin Development and Installation**
Create custom plugins to extend LLE functionality:

```python
# Example: ~/.config/lush/plugins/my_plugin.py
class MyLLEPlugin:
    def on_buffer_changed(self, buffer_content):
        # Custom logic when buffer changes
        if buffer_content.startswith("sudo"):
            return {"suggestion": "Consider using doas instead"}
    
    def custom_completion(self, partial_command):
        # Provide custom completions
        if partial_command.startswith("deploy"):
            return ["deploy staging", "deploy production", "deploy test"]
```

Enable your plugin:
```bash
display lle config set plugin_system true
display lle config set plugins.my_plugin.enabled true
config save
```

#### **Widget Hook Customization**
Create custom editing operations using widget hooks:

```lua
-- ~/.config/lush/widgets/smart_paste.lua
function smart_paste()
    local clipboard = get_clipboard_content()
    if clipboard:match("^https?://") then
        -- URL detected, format as markdown link
        return "[Link](" .. clipboard .. ")"
    else
        return clipboard
    end
end

-- Register the widget
register_widget_hook("ctrl-shift-v", smart_paste)
```

#### **Configuration Management**
Manage all your customizations through the configuration system:

```bash
# View all LLE customizations
config show lle

# Export your complete customization setup
display lle config export ~/.config/lush/my-lle-setup.json

# Import customizations on a new system
display lle config import ~/.config/lush/my-lle-setup.json
config save
```

#### **Scripting Integration Examples**

**Lua Script for Smart History:**
```lua
-- ~/.config/lush/scripts/smart_history.lua
function enhanced_history_search(pattern)
    local matches = search_history(pattern)
    -- Add frequency weighting
    return sort_by_frequency(matches)
end
```

**Python Script for Project-Aware Completions:**
```python
# ~/.config/lush/scripts/project_completion.py
import os
import json

def get_project_completions(partial):
    if os.path.exists('.project.json'):
        with open('.project.json') as f:
            project = json.load(f)
            return project.get('custom_commands', [])
    return []
```

### **Advanced Customization Workflows**

#### **Per-Project Customizations**
Set up project-specific LLE behavior:

```bash
# In project directory
display lle config set project.enable_custom_completions true
display lle config set project.syntax_theme "javascript-enhanced"
config save
```

#### **Team Shared Configurations**
Share customizations across your team:

```bash
# Export team configuration
display lle config export team-lle-config.json

# Team members import
display lle config import team-lle-config.json
config save
```

#### **Performance Monitoring Your Customizations**
Monitor the performance impact of your customizations:

```bash
display lle performance init
# Use your customized LLE for a while
display lle performance report detail       # See impact of customizations
```

---

## **SYSTEM ARCHITECTURE**

### **Core Components Overview**

```
User Customization System Architecture:

+-----------------------------------------------------------------+
|                    LLE USER CUSTOMIZATION SYSTEM                |
+-----------------------------------------------------------------+
|  +-----------------+  +-----------------+  +-----------------+  |
|  |   Key Binding   |  |  Configuration  |  |     Script      |  |
|  |    Manager      |  |    Manager      |  |   Integration   |  |
|  |                 |  |                 |  |                 |  |
|  | * Key Maps      |  | * Schema Valid. |  | * Lua Engine    |  |
|  | * Chord Support |  | * Type Safety   |  | * Python Engine |  |
|  | * Context Aware |  | * Live Reload   |  | * Sandboxing    |  |
|  +-----------------+  +-----------------+  +-----------------+  |
|           |                     |                     |          |
|           +---------------------+---------------------+          |
|                                 |                                |
|  +-----------------+  +-----------------+  +-----------------+  |
|  |    Widget       |  |   Customization |  |   Security &    |  |
|  |   Framework     |  |     Cache       |  |   Permissions   |  |
|  |                 |  |                 |  |                 |  |
|  | * User Widgets  |  | * Fast Lookup   |  | * Sandboxing    |  |
|  | * Edit Ops      |  | * Memory Pool   |  | * Access Control|  |
|  | * Plugin Integration| * Invalidation |  | * Audit Logging |  |
|  +-----------------+  +-----------------+  +-----------------+  |
+-----------------------------------------------------------------+
|              INTEGRATION WITH CORE LLE SYSTEMS                  |
|  Event System | Display | Plugin System | Memory Pool | History |
+-----------------------------------------------------------------+
```

### **Integration Points**

```c
typedef struct lle_customization_system {
    lle_keybinding_manager_t    *keybinding_manager;
    lle_config_manager_t        *config_manager;
    lle_script_integration_t    *script_integration;
    lle_widget_framework_t      *widget_framework;
    lle_customization_cache_t   *cache;
    lle_security_manager_t      *security_manager;
    
    // NEW: Integration systems
    lle_keybinding_integration_t *keybinding_integration; // Integration with default keybindings
    lle_widget_customization_t   *widget_customization;   // Widget hook customization
    lle_completion_customization_t *completion_customization; // Completion menu customization
    
    // Core system integration
    lle_event_system_t          *event_system;
    lle_display_controller_t    *display_controller;
    lle_plugin_manager_t        *plugin_manager;
    lle_memory_pool_t           *memory_pool;
    
    // NEW: Integration coordination state
    lle_customization_mode_t    customization_mode;       // Current customization mode
    bool                        keybinding_mode_active;   // Keybinding mode switching active
    bool                        widget_customization_active; // Widget customization active
    bool                        completion_customization_active; // Completion customization active
    uint64_t                    last_integration_update;  // Last integration state update
    
    // Performance monitoring
    lle_performance_monitor_t   *performance_monitor;
    lle_customization_stats_t   stats;
    
    // Thread safety for integration
    pthread_rwlock_t            integration_lock;         // Integration coordination lock
    
    // State management
    bool                        initialized;
    lle_customization_state_t   state;
} lle_customization_system_t;
```

---

## **KEYBINDING INTEGRATION SYSTEM**

### **Keybinding Integration Architecture**

The user customization system integrates with the Default Keybindings system to provide complete keybinding customization while maintaining GNU Readline compatibility and supporting Emacs/Vi mode switching.

```c
// NEW: Keybinding integration for user customization
typedef struct lle_keybinding_integration {
    // Core integration
    lle_default_keybindings_t *default_keybindings;    // Default keybindings system reference
    lle_custom_keybinding_engine_t *custom_engine;     // Custom keybinding engine
    lle_mode_switching_manager_t *mode_manager;        // Emacs/Vi mode switching
    
    // Keybinding customization
    lle_user_keybinding_registry_t *user_registry;    // User-defined keybindings
    lle_keybinding_override_system_t *override_system; // Override system for custom bindings
    lle_conflict_resolution_t *conflict_resolver;     // Resolve keybinding conflicts
    
    // Mode management
    lle_emacs_mode_customizer_t *emacs_customizer;    // Emacs mode customization
    lle_vi_mode_customizer_t *vi_customizer;          // Vi mode customization
    lle_mode_transition_handler_t *transition_handler; // Handle mode transitions
    
    // Performance optimization
    lle_keybinding_cache_t *custom_binding_cache;     // Custom keybinding cache
    memory_pool_t *keybinding_memory_pool;            // Keybinding-specific memory pool
} lle_keybinding_integration_t;

// NEW: Custom keybinding engine
typedef struct lle_custom_keybinding_engine {
    // User keybinding management
    lle_user_keybinding_parser_t *parser;             // Parse user keybinding definitions
    lle_keybinding_validator_t *validator;            // Validate custom keybindings
    lle_keybinding_compiler_t *compiler;              // Compile keybindings to native format
    
    // Integration with defaults
    lle_default_override_manager_t *override_manager; // Manage overrides of default bindings
    lle_fallback_handler_t *fallback_handler;         // Handle fallbacks to defaults
    lle_compatibility_checker_t *compatibility_checker; // Check GNU Readline compatibility
    
    // Performance metrics
    lle_keybinding_performance_t *metrics;            // Keybinding performance tracking
} lle_custom_keybinding_engine_t;
```

### **Keybinding Integration Implementation**

```c
// NEW: Initialize keybinding integration
lle_result_t lle_customization_init_keybinding_integration(
    lle_customization_system_t *system,
    lle_default_keybindings_t *default_keybindings
) {
    if (!system || !default_keybindings) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Allocate keybinding integration structure
    system->keybinding_integration = memory_pool_allocate(
        system->memory_pool,
        sizeof(lle_keybinding_integration_t)
    );
    
    if (!system->keybinding_integration) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    system->keybinding_integration->default_keybindings = default_keybindings;
    
    // Initialize custom keybinding engine
    lle_result_t result = lle_init_custom_keybinding_engine(
        &system->keybinding_integration->custom_engine,
        system->memory_pool
    );
    
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Initialize mode switching manager
    result = lle_init_mode_switching_manager(
        &system->keybinding_integration->mode_manager,
        default_keybindings,
        system->memory_pool
    );
    
    if (result == LLE_SUCCESS) {
        system->keybinding_mode_active = true;
    }
    
    return result;
}

// NEW: Handle keybinding mode switching (Emacs/Vi)
lle_result_t lle_customization_switch_keybinding_mode(
    lle_customization_system_t *system,
    lle_keybinding_mode_t mode
) {
    if (!system || !system->keybinding_mode_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Apply user customizations to the new mode
    lle_result_t result = lle_apply_user_customizations_to_mode(
        system->keybinding_integration->mode_manager,
        mode,
        system->keybinding_integration->user_registry
    );
    
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Update customization mode
    system->customization_mode = (mode == LLE_KEYBINDING_EMACS_MODE) ? 
                                 LLE_CUSTOM_EMACS_MODE : LLE_CUSTOM_VI_MODE;
    
    system->last_integration_update = lle_get_microsecond_timestamp();
    
    return LLE_SUCCESS;
}
```

---

## **WIDGET HOOK CUSTOMIZATION SYSTEM**

### **Widget Customization Architecture**

The user customization system integrates with the Advanced Prompt Widget Hooks system to provide complete customization of widget behavior, bottom-prompt management, and prompt lifecycle hooks.

```c
// NEW: Widget hook customization system
typedef struct lle_widget_customization {
    // Core integration
    lle_advanced_prompt_widget_hooks_t *widget_hooks; // Widget hook system reference
    lle_user_widget_manager_t *user_widget_manager;   // User-defined widget management
    lle_widget_customization_engine_t *custom_engine; // Widget customization engine
    
    // Widget customization
    lle_bottom_prompt_customizer_t *bottom_customizer; // Bottom-prompt customization
    lle_widget_lifecycle_customizer_t *lifecycle_customizer; // Lifecycle hook customization
    lle_prompt_state_customizer_t *state_customizer;  // Prompt state customization
    
    // User widget support
    lle_custom_widget_registry_t *widget_registry;    // Registry for user widgets
    lle_widget_template_system_t *template_system;    // Widget template system
    lle_widget_scripting_interface_t *script_interface; // Scripting interface for widgets
    
    // Performance optimization
    lle_widget_customization_cache_t *widget_cache;   // Widget customization cache
    memory_pool_t *widget_memory_pool;                // Widget-specific memory pool
} lle_widget_customization_t;

// NEW: Widget customization engine
typedef struct lle_widget_customization_engine {
    // Hook customization
    lle_hook_override_manager_t *hook_override_manager; // Override widget hooks
    lle_hook_callback_customizer_t *callback_customizer; // Customize hook callbacks
    lle_hook_priority_manager_t *priority_manager;     // Manage hook execution priority
    
    // Bottom-prompt customization
    lle_bottom_prompt_theme_manager_t *theme_manager;  // Bottom-prompt theming
    lle_bottom_prompt_layout_manager_t *layout_manager; // Bottom-prompt layout
    lle_historical_prompt_customizer_t *history_customizer; // Historical prompt customization
    
    // Performance metrics
    lle_widget_customization_metrics_t *metrics;      // Widget customization performance
} lle_widget_customization_engine_t;
```

---

## **COMPLETION MENU CUSTOMIZATION SYSTEM**

### **Completion Customization Architecture**

The user customization system integrates with the Interactive Completion Menu system to provide complete customization of completion categories, ranking algorithms, and visual presentation.

```c
// NEW: Completion menu customization system
typedef struct lle_completion_customization {
    // Core integration
    lle_interactive_completion_menu_t *completion_menu; // Completion menu system reference
    lle_user_completion_manager_t *user_completion_manager; // User completion management
    lle_completion_customization_engine_t *custom_engine; // Completion customization engine
    
    // Category customization
    lle_category_customizer_t *category_customizer;   // Customize completion categories
    lle_ranking_customizer_t *ranking_customizer;     // Customize ranking algorithms
    lle_visual_customizer_t *visual_customizer;       // Customize visual presentation
    
    // Custom completion sources
    lle_custom_completion_registry_t *completion_registry; // Registry for custom completions
    lle_completion_script_interface_t *script_interface; // Scripting interface for completions
    lle_completion_template_system_t *template_system; // Completion template system
    
    // Performance optimization
    lle_completion_customization_cache_t *completion_cache; // Completion customization cache
    memory_pool_t *completion_memory_pool;            // Completion-specific memory pool
} lle_completion_customization_t;

// NEW: Completion customization engine
typedef struct lle_completion_customization_engine {
    // Menu customization
    lle_menu_layout_customizer_t *layout_customizer;  // Customize menu layout
    lle_menu_navigation_customizer_t *nav_customizer; // Customize navigation behavior
    lle_menu_theme_customizer_t *theme_customizer;    // Customize menu theming
    
    // Ranking customization
    lle_custom_ranking_engine_t *ranking_engine;      // Custom ranking algorithms
    lle_preference_learning_t *preference_learning;   // Learn user preferences
    lle_context_ranking_t *context_ranking;           // Context-aware ranking
    
    // Performance metrics
    lle_completion_customization_metrics_t *metrics;  // Completion customization performance
} lle_completion_customization_engine_t;
```

### **Widget Customization Implementation**

```c
// NEW: Initialize widget hook customization
lle_result_t lle_customization_init_widget_customization(
    lle_customization_system_t *system,
    lle_advanced_prompt_widget_hooks_t *widget_hooks
) {
    if (!system || !widget_hooks) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Allocate widget customization structure
    system->widget_customization = memory_pool_allocate(
        system->memory_pool,
        sizeof(lle_widget_customization_t)
    );
    
    if (!system->widget_customization) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    system->widget_customization->widget_hooks = widget_hooks;
    
    // Initialize widget customization engine
    lle_result_t result = lle_init_widget_customization_engine(
        &system->widget_customization->custom_engine,
        system->memory_pool
    );
    
    if (result == LLE_SUCCESS) {
        system->widget_customization_active = true;
    }
    
    return result;
}

// NEW: Customize bottom-prompt behavior
lle_result_t lle_customization_customize_bottom_prompt(
    lle_customization_system_t *system,
    lle_bottom_prompt_config_t *config
) {
    if (!system || !config || !system->widget_customization_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Apply bottom-prompt customizations
    return lle_apply_bottom_prompt_customizations(
        system->widget_customization->bottom_customizer,
        config
    );
}
```

### **Completion Customization Implementation**

```c
// NEW: Initialize completion menu customization
lle_result_t lle_customization_init_completion_customization(
    lle_customization_system_t *system,
    lle_interactive_completion_menu_t *completion_menu
) {
    if (!system || !completion_menu) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Allocate completion customization structure
    system->completion_customization = memory_pool_allocate(
        system->memory_pool,
        sizeof(lle_completion_customization_t)
    );
    
    if (!system->completion_customization) {
        return LLE_ERROR_MEMORY_ALLOCATION;
    }
    
    system->completion_customization->completion_menu = completion_menu;
    
    // Initialize completion customization engine
    lle_result_t result = lle_init_completion_customization_engine(
        &system->completion_customization->custom_engine,
        system->memory_pool
    );
    
    if (result == LLE_SUCCESS) {
        system->completion_customization_active = true;
    }
    
    return result;
}

// NEW: Customize completion categories
lle_result_t lle_customization_customize_completion_categories(
    lle_customization_system_t *system,
    lle_completion_category_config_t *category_config
) {
    if (!system || !category_config || !system->completion_customization_active) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Apply completion category customizations
    return lle_apply_completion_category_customizations(
        system->completion_customization->category_customizer,
        category_config
    );
}
```

### **Comprehensive System Initialization**

```c
// NEW: Initialize customization system with full integration support
lle_result_t lle_customization_system_init_with_integrations(
    lle_customization_system_t **system,
    memory_pool_t *memory_pool,
    lle_default_keybindings_t *default_keybindings,
    lle_advanced_prompt_widget_hooks_t *widget_hooks,
    lle_interactive_completion_menu_t *completion_menu
) {
    // Initialize base system first
    lle_result_t result = lle_customization_system_init(
        system, 
        memory_pool
    );
    
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Initialize keybinding integration if provided
    if (default_keybindings) {
        result = lle_customization_init_keybinding_integration(
            *system, 
            default_keybindings
        );
        
        if (result != LLE_SUCCESS) {
            lle_customization_system_destroy(*system);
            *system = NULL;
            return result;
        }
    }
    
    // Initialize widget customization if provided
    if (widget_hooks) {
        result = lle_customization_init_widget_customization(
            *system, 
            widget_hooks
        );
        
        if (result != LLE_SUCCESS) {
            lle_customization_system_destroy(*system);
            *system = NULL;
            return result;
        }
    }
    
    // Initialize completion customization if provided
    if (completion_menu) {
        result = lle_customization_init_completion_customization(
            *system, 
            completion_menu
        );
        
        if (result != LLE_SUCCESS) {
            lle_customization_system_destroy(*system);
            *system = NULL;
            return result;
        }
    }
    
    return LLE_SUCCESS;
}
```

### **Integration Performance Requirements**

- **Keybinding Mode Switching**: <20us for switching between Emacs/Vi modes
- **Widget Customization Loading**: <100us for applying widget customizations
- **Completion Category Customization**: <50us for applying category configurations
- **Total Integration Overhead**: <200us maintaining sub-500us customization targets

---

## **KEY BINDING MANAGEMENT SYSTEM (ENHANCED)**

### **Key Binding Architecture**

The key binding system provides complete control over key mappings with support for:
- Single key bindings
- Key chords and sequences  
- Context-sensitive bindings
- Mode-specific bindings
- Dynamic binding updates

```c
typedef enum {
    LLE_KEYMAP_INSERT,      // Insert mode bindings
    LLE_KEYMAP_NORMAL,      // Normal mode bindings (vi-style)
    LLE_KEYMAP_VISUAL,      // Visual mode bindings
    LLE_KEYMAP_SEARCH,      // Search mode bindings
    LLE_KEYMAP_COMPLETION,  // Completion mode bindings
    LLE_KEYMAP_GLOBAL,      // Global bindings (active in all modes)
    LLE_KEYMAP_CONTEXT,     // Context-specific bindings
    LLE_KEYMAP_COUNT
} lle_keymap_type_t;

typedef struct {
    char                    *sequence;      // Key sequence (e.g., "Ctrl+x Ctrl+s")
    size_t                  sequence_len;   // Sequence length
    lle_keymap_type_t       keymap;        // Target keymap
    char                    *context;       // Context filter (optional)
    lle_key_action_type_t   action_type;   // Action type
    union {
        lle_builtin_action_t    builtin;    // Built-in action
        lle_user_widget_t       *widget;   // User-defined widget
        lle_script_function_t   script;     // Script function
        lle_plugin_action_t     plugin;     // Plugin action
    } action;
    
    // Metadata
    char                    *description;   // User-friendly description
    uint32_t                priority;      // Binding priority
    bool                    enabled;       // Runtime enable/disable
    uint64_t                created_time;  // Creation timestamp
    uint64_t                last_used;     // Last usage timestamp
    uint32_t                use_count;     // Usage counter
} lle_key_binding_t;
```

### **Key Binding Manager Implementation**

```c
typedef struct {
    // Keymaps for each mode
    lle_hashtable_t         *keymaps[LLE_KEYMAP_COUNT];
    
    // Chord handling
    lle_key_chord_state_t   chord_state;
    char                    *chord_buffer;
    size_t                  chord_buffer_len;
    size_t                  chord_buffer_capacity;
    uint64_t                chord_timeout_ms;
    
    // Context system
    lle_hashtable_t         *context_filters;
    char                    *current_context;
    
    // Performance optimization
    lle_hashtable_t         *binding_cache;
    lle_key_sequence_trie_t *sequence_trie;    // Fast prefix matching
    
    // Dynamic updates
    lle_config_watcher_t    *config_watcher;
    bool                    auto_reload;
    
    // Statistics
    lle_keybinding_stats_t  stats;
    
    // Memory management
    lle_memory_pool_t       *memory_pool;
} lle_keybinding_manager_t;

// Initialize key binding manager
lle_result_t lle_keybinding_manager_init(
    lle_keybinding_manager_t **manager,
    lle_memory_pool_t *memory_pool,
    lle_config_manager_t *config_manager
) {
    // Step 1: Allocate manager structure
    *manager = lle_memory_pool_alloc(memory_pool, sizeof(lle_keybinding_manager_t));
    if (!*manager) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    
    lle_keybinding_manager_t *mgr = *manager;
    memset(mgr, 0, sizeof(lle_keybinding_manager_t));
    mgr->memory_pool = memory_pool;
    
    // Step 2: Initialize keymaps for each mode
    for (int i = 0; i < LLE_KEYMAP_COUNT; i++) {
        lle_result_t result = lle_hashtable_create(
            &mgr->keymaps[i],
            256,  // Initial capacity
            lle_hash_string,
            lle_compare_string,
            memory_pool
        );
        if (result != LLE_SUCCESS) {
            lle_keybinding_manager_destroy(mgr);
            return result;
        }
    }
    
    // Step 3: Initialize chord handling
    mgr->chord_buffer_capacity = 1024;
    mgr->chord_buffer = lle_memory_pool_alloc(memory_pool, mgr->chord_buffer_capacity);
    if (!mgr->chord_buffer) {
        lle_keybinding_manager_destroy(mgr);
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    mgr->chord_timeout_ms = 1000; // 1 second timeout
    
    // Step 4: Initialize context system
    lle_result_t result = lle_hashtable_create(
        &mgr->context_filters,
        64,
        lle_hash_string,
        lle_compare_string,
        memory_pool
    );
    if (result != LLE_SUCCESS) {
        lle_keybinding_manager_destroy(mgr);
        return result;
    }
    
    // Step 5: Initialize performance optimization structures
    result = lle_hashtable_create(
        &mgr->binding_cache,
        512,
        lle_hash_string,
        lle_compare_string,
        memory_pool
    );
    if (result != LLE_SUCCESS) {
        lle_keybinding_manager_destroy(mgr);
        return result;
    }
    
    result = lle_key_sequence_trie_create(&mgr->sequence_trie, memory_pool);
    if (result != LLE_SUCCESS) {
        lle_keybinding_manager_destroy(mgr);
        return result;
    }
    
    // Step 6: Load default bindings
    result = lle_keybinding_manager_load_defaults(mgr);
    if (result != LLE_SUCCESS) {
        lle_keybinding_manager_destroy(mgr);
        return result;
    }
    
    return LLE_SUCCESS;
}

// Bind key sequence to action
lle_result_t lle_keybinding_manager_bind_key(
    lle_keybinding_manager_t *manager,
    const char *key_sequence,
    lle_keymap_type_t keymap,
    const char *context,
    lle_key_action_type_t action_type,
    void *action_data,
    const char *description
) {
    // Step 1: Validate input parameters
    if (!manager || !key_sequence || keymap >= LLE_KEYMAP_COUNT) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Step 2: Parse key sequence
    lle_key_sequence_t *sequence = NULL;
    lle_result_t result = lle_parse_key_sequence(key_sequence, &sequence, manager->memory_pool);
    if (result != LLE_SUCCESS) {
        return result;
    }
    
    // Step 3: Validate key sequence
    result = lle_validate_key_sequence(sequence);
    if (result != LLE_SUCCESS) {
        lle_key_sequence_destroy(sequence);
        return result;
    }
    
    // Step 4: Create binding structure
    lle_key_binding_t *binding = lle_memory_pool_alloc(
        manager->memory_pool, 
        sizeof(lle_key_binding_t)
    );
    if (!binding) {
        lle_key_sequence_destroy(sequence);
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    
    // Step 5: Initialize binding
    memset(binding, 0, sizeof(lle_key_binding_t));
    binding->sequence = lle_string_duplicate(key_sequence, manager->memory_pool);
    binding->sequence_len = strlen(key_sequence);
    binding->keymap = keymap;
    binding->action_type = action_type;
    binding->enabled = true;
    binding->created_time = lle_get_timestamp_us();
    binding->priority = 1000; // Default priority
    
    if (context) {
        binding->context = lle_string_duplicate(context, manager->memory_pool);
    }
    
    if (description) {
        binding->description = lle_string_duplicate(description, manager->memory_pool);
    }
    
    // Step 6: Set action data based on type
    switch (action_type) {
        case LLE_ACTION_BUILTIN:
            binding->action.builtin = *(lle_builtin_action_t*)action_data;
            break;
        case LLE_ACTION_USER_WIDGET:
            binding->action.widget = (lle_user_widget_t*)action_data;
            break;
        case LLE_ACTION_SCRIPT:
            binding->action.script = *(lle_script_function_t*)action_data;
            break;
        case LLE_ACTION_PLUGIN:
            binding->action.plugin = *(lle_plugin_action_t*)action_data;
            break;
        default:
            lle_key_binding_destroy(binding);
            lle_key_sequence_destroy(sequence);
            return LLE_ERROR_INVALID_PARAMETER;
    }
    
    // Step 7: Add to appropriate keymap
    result = lle_hashtable_insert(
        manager->keymaps[keymap],
        binding->sequence,
        binding
    );
    if (result != LLE_SUCCESS) {
        lle_key_binding_destroy(binding);
        lle_key_sequence_destroy(sequence);
        return result;
    }
    
    // Step 8: Add to sequence trie for fast prefix matching
    result = lle_key_sequence_trie_insert(
        manager->sequence_trie,
        sequence,
        binding
    );
    if (result != LLE_SUCCESS) {
        lle_hashtable_remove(manager->keymaps[keymap], binding->sequence);
        lle_key_binding_destroy(binding);
        lle_key_sequence_destroy(sequence);
        return result;
    }
    
    // Step 9: Invalidate cache
    lle_hashtable_clear(manager->binding_cache);
    
    // Step 10: Update statistics
    manager->stats.total_bindings++;
    manager->stats.bindings_by_keymap[keymap]++;
    
    lle_key_sequence_destroy(sequence);
    return LLE_SUCCESS;
}

// Lookup key binding with full context resolution
lle_result_t lle_keybinding_manager_lookup(
    lle_keybinding_manager_t *manager,
    const char *key_sequence,
    lle_keymap_type_t current_keymap,
    const char *current_context,
    lle_key_binding_t **binding_out
) {
    // Step 1: Validate parameters
    if (!manager || !key_sequence || !binding_out) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    *binding_out = NULL;
    
    // Step 2: Check cache first
    char cache_key[512];
    snprintf(cache_key, sizeof(cache_key), "%s:%d:%s", 
             key_sequence, current_keymap, current_context ? current_context : "");
    
    lle_key_binding_t *cached_binding = NULL;
    if (lle_hashtable_get(manager->binding_cache, cache_key, (void**)&cached_binding) == LLE_SUCCESS) {
        if (cached_binding && cached_binding->enabled) {
            *binding_out = cached_binding;
            manager->stats.cache_hits++;
            return LLE_SUCCESS;
        }
    }
    
    manager->stats.cache_misses++;
    
    // Step 3: Search in priority order
    lle_keymap_type_t search_order[] = {
        current_keymap,     // Current mode first
        LLE_KEYMAP_CONTEXT, // Context-specific
        LLE_KEYMAP_GLOBAL   // Global fallback
    };
    
    lle_key_binding_t *best_binding = NULL;
    uint32_t best_priority = 0;
    
    for (size_t i = 0; i < sizeof(search_order) / sizeof(search_order[0]); i++) {
        lle_keymap_type_t keymap = search_order[i];
        
        lle_key_binding_t *binding = NULL;
        if (lle_hashtable_get(manager->keymaps[keymap], key_sequence, (void**)&binding) == LLE_SUCCESS) {
            if (!binding || !binding->enabled) {
                continue;
            }
            
            // Step 4: Check context filter
            if (binding->context) {
                if (!current_context || !lle_context_matches(binding->context, current_context)) {
                    continue;
                }
            }
            
            // Step 5: Check priority
            if (!best_binding || binding->priority > best_priority) {
                best_binding = binding;
                best_priority = binding->priority;
            }
        }
    }
    
    // Step 6: Cache result
    if (best_binding) {
        lle_hashtable_insert(manager->binding_cache, cache_key, best_binding);
        *binding_out = best_binding;
        
        // Step 7: Update usage statistics
        best_binding->last_used = lle_get_timestamp_us();
        best_binding->use_count++;
        manager->stats.successful_lookups++;
        
        return LLE_SUCCESS;
    }
    
    manager->stats.failed_lookups++;
    return LLE_ERROR_KEY_NOT_BOUND;
}
```

### **Key Chord and Sequence Handling**

```c
typedef enum {
    LLE_CHORD_STATE_IDLE,       // No chord in progress
    LLE_CHORD_STATE_BUILDING,   // Building chord sequence
    LLE_CHORD_STATE_COMPLETE,   // Chord sequence complete
    LLE_CHORD_STATE_TIMEOUT     // Chord sequence timed out
} lle_chord_state_type_t;

typedef struct {
    lle_chord_state_type_t  state;
    char                    *buffer;
    size_t                  buffer_len;
    size_t                  buffer_capacity;
    uint64_t                start_time;
    uint64_t                timeout_ms;
    lle_key_binding_t       *partial_matches[32];
    size_t                  partial_match_count;
} lle_key_chord_state_t;

// Process key input for chord handling
lle_result_t lle_keybinding_process_key_input(
    lle_keybinding_manager_t *manager,
    const lle_input_event_t *event,
    lle_keymap_type_t current_keymap,
    const char *current_context,
    lle_key_binding_result_t *result_out
) {
    // Step 1: Validate parameters
    if (!manager || !event || !result_out) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    
    result_out->action_type = LLE_KEY_ACTION_NONE;
    result_out->binding = NULL;
    
    // Step 2: Convert input event to key string
    char key_string[64];
    lle_result_t conversion_result = lle_input_event_to_key_string(event, key_string, sizeof(key_string));
    if (conversion_result != LLE_SUCCESS) {
        return conversion_result;
    }
    
    // Step 3: Handle chord state
    if (manager->chord_state.state == LLE_CHORD_STATE_IDLE) {
        // Starting new potential chord
        manager->chord_state.state = LLE_CHORD_STATE_BUILDING;
        manager->chord_state.buffer_len = 0;
        manager->chord_state.start_time = lle_get_timestamp_us();
        manager->chord_state.partial_match_count = 0;
    }
    
    // Step 4: Check for chord timeout
    uint64_t current_time = lle_get_timestamp_us();
    if (current_time - manager->chord_state.start_time > manager->chord_timeout_ms * 1000) {
        // Timeout reached, reset chord state
        lle_keybinding_chord_reset(manager);
        manager->chord_state.state = LLE_CHORD_STATE_BUILDING;
        manager->chord_state.buffer_len = 0;
        manager->chord_state.start_time = current_time;
    }
    
    // Step 5: Append key to chord buffer
    size_t key_len = strlen(key_string);
    if (manager->chord_state.buffer_len + key_len + 1 >= manager->chord_state.buffer_capacity) {
        // Buffer overflow protection
        lle_keybinding_chord_reset(manager);
        result_out->action_type = LLE_KEY_ACTION_ERROR;
        return LLE_ERROR_BUFFER_OVERFLOW;
    }
    
    if (manager->chord_state.buffer_len > 0) {
        manager->chord_state.buffer[manager->chord_state.buffer_len++] = ' ';
    }
    strcpy(manager->chord_state.buffer + manager->chord_state.buffer_len, key_string);
    manager->chord_state.buffer_len += key_len;
    
    // Step 6: Look for exact match
    lle_key_binding_t *exact_binding = NULL;
    lle_result_t lookup_result = lle_keybinding_manager_lookup(
        manager,
        manager->chord_state.buffer,
        current_keymap,
        current_context,
        &exact_binding
    );
    
    if (lookup_result == LLE_SUCCESS && exact_binding) {
        // Found exact match
        result_out->action_type = LLE_KEY_ACTION_EXECUTE;
        result_out->binding = exact_binding;
        lle_keybinding_chord_reset(manager);
        return LLE_SUCCESS;
    }
    
    // Step 7: Look for partial matches
    size_t partial_count = 0;
    lle_result_t partial_result = lle_keybinding_find_partial_matches(
        manager,
        manager->chord_state.buffer,
        current_keymap,
        current_context,
        manager->chord_state.partial_matches,
        sizeof(manager->chord_state.partial_matches) / sizeof(manager->chord_state.partial_matches[0]),
        &partial_count
    );
    
    if (partial_result == LLE_SUCCESS && partial_count > 0) {
        // Found partial matches, continue chord
        manager->chord_state.partial_match_count = partial_count;
        result_out->action_type = LLE_KEY_ACTION_PARTIAL;
        result_out->partial_matches = manager->chord_state.partial_matches;
        result_out->partial_count = partial_count;
        return LLE_SUCCESS;
    }
    
    // Step 8: No matches found, reset and try single key
    lle_keybinding_chord_reset(manager);
    
    lookup_result = lle_keybinding_manager_lookup(
        manager,
        key_string,
        current_keymap,
        current_context,
        &exact_binding
    );
    
    if (lookup_result == LLE_SUCCESS && exact_binding) {
        result_out->action_type = LLE_KEY_ACTION_EXECUTE;
        result_out->binding = exact_binding;
        return LLE_SUCCESS;
    }
    
    // Step 9: No binding found
    result_out->action_type = LLE_KEY_ACTION_NONE;
    manager->stats.unbound_keys++;
    return LLE_SUCCESS;
}
```

---

## **CONFIGURATION MANAGEMENT SYSTEM**

### **Configuration Architecture**

The configuration system provides type-safe, schema-validated configuration management with real-time updates and comprehensive error handling.

```c
typedef enum {
    LLE_CONFIG_TYPE_BOOLEAN,
    LLE_CONFIG_TYPE_INTEGER,
    LLE_CONFIG_TYPE_FLOAT,
    LLE_CONFIG_TYPE_STRING,
    LLE_CONFIG_TYPE_ARRAY,
    LLE_CONFIG_TYPE_OBJECT,
    LLE_CONFIG_TYPE_COLOR,
    LLE_CONFIG_TYPE_KEYBINDING,
    LLE_CONFIG_TYPE_SCRIPT_PATH,
    LLE_CONFIG_TYPE_ENUM
} lle_config_value_type_t;

typedef struct lle_config_value {
    lle_config_value_type_t type;
    char                    *key;
    char                    *description;
    bool                    required;
    bool                    readonly;
    
    union {
        bool                boolean_val;
        int64_t             integer_val;
        double              float_val;
        char                *string_val;
        struct {
            struct lle_config_value **items;
            size_t              count;
        } array_val;
        struct {
            lle_hashtable_t     *properties;
        } object_val;
        lle_color_t         color_val;
        lle_key_binding_t   *keybinding_val;
        struct {
            char                **allowed_values;
            size_t              allowed_count;
            size_t              selected_index;
        } enum_val;
    } value;
    
    // Validation
    lle_config_validator_t  *validator;
    
    // Metadata
    uint64_t                last_modified;
    char                    *source_file;
    uint32_t                source_line;
    bool                    user_modified;
} lle_config_value_t;

typedef struct {
    // Configuration storage
    lle_hashtable_t         *config_tree;      // Hierarchical config tree
    lle_hashtable_t         *flat_index;       // Flat key->value index for fast access
    lle_config_schema_t     *schema;           // Configuration schema
    
    // File management
    char                    **config_files;    // Configuration file paths
    size_t                  config_file_count;
    lle_file_watcher_t      *file_watcher;     // Auto-reload on changes
    
    // Change tracking
    lle_config_change_log_t *change_log;       // Track all configuration changes
    lle_hashtable_t         *pending_changes;  // Pending changes before commit
    bool                    auto_save;
    
    // Performance optimization
    lle_hashtable_t         *value_cache;      // Cached resolved values
    uint64_t                cache_version;
    
    // Event system integration
    lle_event_system_t      *event_system;
    lle_config_event_handlers_t event_handlers;
    
    // Memory management
    lle_memory_pool_t       *memory_pool;
    
    // Statistics
    lle_config_stats_t      stats;
} lle_config_manager_t;
```

### **Configuration Manager Implementation**

```c
// Initialize configuration manager
lle_result_t lle_config_manager_init(
    lle_config_manager_t **manager,
    lle_memory_pool_t *memory_pool,
    lle_event_system_t *event_system,
    const char *schema_file,
    const char **config_files,
    size_t config_file_count
) {
    // Step 1: Allocate manager structure
    *manager = lle_memory_pool_alloc(memory_pool, sizeof(lle_config_manager_t));
    if (!*manager) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }
    
    lle_config_manager_t *mgr = *manager;
    memset(mgr, 0, sizeof(lle_config_manager_t));
    mgr->memory_pool = memory_pool;
    mgr->event_system = event_system;
    mgr->auto_save = true;
    
    // Step 2: Initialize storage structures
    lle_result_t result = lle_hashtable_create(
        &mgr->config_tree,
        256,
        lle_hash_string,
        lle_compare_string,
        memory_pool
    );
    if (result != LLE_SUCCESS) {
        lle_config_manager_destroy(mgr);
        return result;
    }
    
    result = lle_hashtable_create(
        &mgr->flat_index,
        1024,
        lle_hash_string,
        lle_compare_string,
        memory_pool
    );
    if (result != LLE_SUCCESS) {
        lle_config_manager_destroy(mgr);
        return result;
    }
    
    result = lle_hashtable_create(
        &mgr->pending_changes,
        64,
        lle_hash_string,
        lle_compare_string,
        memory_pool
    );
    if (result != LLE_SUCCESS) {
        lle_config_manager_destroy(mgr);
        return result;
    }
    
    result = lle_hashtable_create(
        &mgr->value_cache,
        512,
        lle_hash_string,
        lle_compare_string,
        memory_pool
    );
    if (result != LLE_SUCCESS) {
        lle_config_manager_destroy(mgr);
        return result;
    }
    
    // Step 3: Load configuration schema
    if (schema_file) {
        result = lle_config_schema_load(&mgr->schema, schema_file, memory_pool);
        if (result != LLE_SUCCESS) {
            lle_config_manager_destroy(mgr);
            return result;
        }
    }
    
    // Step 4: Copy configuration file paths
    if (config_files && config_file_count > 0) {
        mgr->config_files = lle_memory_pool_alloc(
            memory_pool,
            config_file_count * sizeof(char*)
        );
        if (!mgr->config_files) {
            lle_config_manager_destroy(mgr);
            return LLE_ERROR_OUT_OF_MEMORY;
        }
        
        for (size_t i = 0; i < config_file_count; i++) {
            mgr->config_files[i] = lle_string_duplicate(config_files[i], memory_pool);
            if (!mgr->config_files[i]) {
                lle_config_manager_destroy(mgr);
                return LLE_ERROR_OUT_OF_MEMORY;
            }
            }
            mgr->config_file_count = config_file_count;
        }
    
        // Step 5: Initialize change log
        result = lle_config_change_log_init(&mgr->change_log, memory_pool);
        if (result != LLE_SUCCESS) {
            lle_config_manager_destroy(mgr);
            return result;
        }
    
        // Step 6: Initialize file watcher for auto-reload
        result = lle_file_watcher_init(&mgr->file_watcher, memory_pool);
        if (result != LLE_SUCCESS) {
            lle_config_manager_destroy(mgr);
            return result;
        }
    
        // Step 7: Load configuration files
        for (size_t i = 0; i < mgr->config_file_count; i++) {
            result = lle_config_manager_load_file(mgr, mgr->config_files[i]);
            if (result != LLE_SUCCESS) {
                // Log error but continue with other files
                lle_log_warn("Failed to load config file: %s", mgr->config_files[i]);
            }
        }
    
        // Step 8: Setup file watchers
        for (size_t i = 0; i < mgr->config_file_count; i++) {
            lle_file_watcher_add_path(mgr->file_watcher, mgr->config_files[i]);
        }
    
        // Step 9: Validate loaded configuration against schema
        if (mgr->schema) {
            result = lle_config_manager_validate_all(mgr);
            if (result != LLE_SUCCESS) {
                lle_log_warn("Configuration validation failed");
            }
        }
    
        return LLE_SUCCESS;
    }

    // Get configuration value with type checking
    lle_result_t lle_config_manager_get_value(
        lle_config_manager_t *manager,
        const char *key,
        lle_config_value_type_t expected_type,
        lle_config_value_t **value_out
    ) {
        // Step 1: Validate parameters
        if (!manager || !key || !value_out) {
            return LLE_ERROR_INVALID_PARAMETER;
        }
    
        *value_out = NULL;
    
        // Step 2: Check cache first
        char cache_key[512];
        snprintf(cache_key, sizeof(cache_key), "%s:%d", key, expected_type);
    
        lle_config_value_t *cached_value = NULL;
        if (lle_hashtable_get(manager->value_cache, cache_key, (void**)&cached_value) == LLE_SUCCESS) {
            if (cached_value) {
                *value_out = cached_value;
                manager->stats.cache_hits++;
                return LLE_SUCCESS;
            }
        }
    
        manager->stats.cache_misses++;
    
        // Step 3: Look up in flat index
        lle_config_value_t *value = NULL;
        lle_result_t result = lle_hashtable_get(manager->flat_index, key, (void**)&value);
        if (result != LLE_SUCCESS || !value) {
            manager->stats.key_not_found++;
            return LLE_ERROR_CONFIG_KEY_NOT_FOUND;
        }
    
        // Step 4: Type checking
        if (value->type != expected_type) {
            manager->stats.type_mismatches++;
            return LLE_ERROR_CONFIG_TYPE_MISMATCH;
        }
    
        // Step 5: Cache the result
        lle_hashtable_insert(manager->value_cache, cache_key, value);
    
        // Step 6: Update access statistics
        value->last_modified = lle_get_timestamp_us();
        manager->stats.successful_gets++;
    
        *value_out = value;
        return LLE_SUCCESS;
    }

    // Set configuration value with validation
    lle_result_t lle_config_manager_set_value(
        lle_config_manager_t *manager,
        const char *key,
        lle_config_value_type_t type,
        const void *value_data,
        bool immediate_save
    ) {
        // Step 1: Validate parameters
        if (!manager || !key || !value_data) {
            return LLE_ERROR_INVALID_PARAMETER;
        }
    
        // Step 2: Check if key is readonly
        lle_config_value_t *existing_value = NULL;
        if (lle_hashtable_get(manager->flat_index, key, (void**)&existing_value) == LLE_SUCCESS) {
            if (existing_value && existing_value->readonly) {
                return LLE_ERROR_CONFIG_READONLY;
            }
        }
    
        // Step 3: Create new value structure
        lle_config_value_t *new_value = lle_memory_pool_alloc(
            manager->memory_pool,
            sizeof(lle_config_value_t)
        );
        if (!new_value) {
            return LLE_ERROR_OUT_OF_MEMORY;
        }
    
        memset(new_value, 0, sizeof(lle_config_value_t));
        new_value->type = type;
        new_value->key = lle_string_duplicate(key, manager->memory_pool);
        new_value->last_modified = lle_get_timestamp_us();
        new_value->user_modified = true;
    
        // Step 4: Set value based on type
        lle_result_t result = LLE_SUCCESS;
        switch (type) {
            case LLE_CONFIG_TYPE_BOOLEAN:
                new_value->value.boolean_val = *(bool*)value_data;
                break;
            case LLE_CONFIG_TYPE_INTEGER:
                new_value->value.integer_val = *(int64_t*)value_data;
                break;
            case LLE_CONFIG_TYPE_FLOAT:
                new_value->value.float_val = *(double*)value_data;
                break;
            case LLE_CONFIG_TYPE_STRING:
                new_value->value.string_val = lle_string_duplicate((char*)value_data, manager->memory_pool);
                if (!new_value->value.string_val) {
                    result = LLE_ERROR_OUT_OF_MEMORY;
                }
                break;
            case LLE_CONFIG_TYPE_COLOR:
                new_value->value.color_val = *(lle_color_t*)value_data;
                break;
            default:
                result = LLE_ERROR_INVALID_PARAMETER;
                break;
        }
    
        if (result != LLE_SUCCESS) {
            lle_config_value_destroy(new_value);
            return result;
        }
    
        // Step 5: Validate against schema
        if (manager->schema) {
            result = lle_config_validate_value(manager->schema, new_value);
            if (result != LLE_SUCCESS) {
                lle_config_value_destroy(new_value);
                return result;
            }
        }
    
        // Step 6: Add to pending changes
        result = lle_hashtable_insert(manager->pending_changes, key, new_value);
        if (result != LLE_SUCCESS) {
            lle_config_value_destroy(new_value);
            return result;
        }
    
        // Step 7: Immediately apply if requested
        if (immediate_save) {
            result = lle_config_manager_commit_changes(manager);
            if (result != LLE_SUCCESS) {
                lle_hashtable_remove(manager->pending_changes, key);
                return result;
            }
        }
    
        // Step 8: Invalidate cache
        lle_hashtable_clear(manager->value_cache);
        manager->cache_version++;
    
        // Step 9: Fire change event
        lle_config_change_event_t change_event = {
            .key = key,
            .old_value = existing_value,
            .new_value = new_value,
            .timestamp = lle_get_timestamp_us()
        };
    
        lle_event_t event = {
            .type = LLE_EVENT_CONFIG_CHANGED,
            .data = &change_event,
            .data_size = sizeof(change_event)
        };
    
        lle_event_system_dispatch(manager->event_system, &event);
    
        manager->stats.successful_sets++;
        return LLE_SUCCESS;
    }
    ```

    ---

    ## **SCRIPT INTEGRATION SYSTEM**

    The script integration system provides seamless Lua and Python scripting support for advanced customization capabilities.

    ### **Script Engine Architecture**

    ```c
    typedef enum {
        LLE_SCRIPT_ENGINE_LUA,
        LLE_SCRIPT_ENGINE_PYTHON,
        LLE_SCRIPT_ENGINE_COUNT
    } lle_script_engine_type_t;

    typedef struct {
        lle_script_engine_type_t    engine_type;
        void                        *engine_state;     // lua_State* or PyObject*
        lle_hashtable_t             *function_registry; // Available functions
        lle_hashtable_t             *global_variables;  // Global script variables
    
        // Security
        lle_script_sandbox_t        *sandbox;
        lle_script_permissions_t    permissions;
    
        // Performance
        lle_script_cache_t          *bytecode_cache;
        lle_execution_stats_t       stats;
    
        // Memory management  
        lle_memory_pool_t           *memory_pool;
        size_t                      memory_limit;
        size_t                      memory_used;
    
        // Error handling
        lle_script_error_handler_t  error_handler;
        char                        *last_error;
    } lle_script_engine_t;

    typedef struct {
        // Engine management
        lle_script_engine_t         *engines[LLE_SCRIPT_ENGINE_COUNT];
        lle_script_engine_type_t    default_engine;
    
        // Script management
        lle_hashtable_t             *loaded_scripts;
        lle_hashtable_t             *script_metadata;
    
        // API exposure
        lle_script_api_registry_t   *api_registry;
        lle_customization_system_t  *customization_system;
    
        // Security management
        lle_security_manager_t      *security_manager;
        bool                        sandboxing_enabled;
    
        // Performance monitoring
        lle_performance_monitor_t   *performance_monitor;
        lle_script_integration_stats_t stats;
    
        // Memory management
        lle_memory_pool_t           *memory_pool;
    } lle_script_integration_t;
    ```

    ### **Script Integration Implementation**

    ```c
    // Initialize script integration system
    lle_result_t lle_script_integration_init(
        lle_script_integration_t **integration,
        lle_memory_pool_t *memory_pool,
        lle_security_manager_t *security_manager,
        lle_customization_system_t *customization_system
    ) {
        // Step 1: Allocate integration structure
        *integration = lle_memory_pool_alloc(memory_pool, sizeof(lle_script_integration_t));
        if (!*integration) {
            return LLE_ERROR_OUT_OF_MEMORY;
        }
    
        lle_script_integration_t *si = *integration;
        memset(si, 0, sizeof(lle_script_integration_t));
        si->memory_pool = memory_pool;
        si->security_manager = security_manager;
        si->customization_system = customization_system;
        si->default_engine = LLE_SCRIPT_ENGINE_LUA;
        si->sandboxing_enabled = true;
    
        // Step 2: Initialize script storage
        lle_result_t result = lle_hashtable_create(
            &si->loaded_scripts,
            128,
            lle_hash_string,
            lle_compare_string,
            memory_pool
        );
        if (result != LLE_SUCCESS) {
            lle_script_integration_destroy(si);
            return result;
        }
    
        result = lle_hashtable_create(
            &si->script_metadata,
            128,
            lle_hash_string,
            lle_compare_string,
            memory_pool
        );
        if (result != LLE_SUCCESS) {
            lle_script_integration_destroy(si);
            return result;
        }
    
        // Step 3: Initialize script engines
        for (int i = 0; i < LLE_SCRIPT_ENGINE_COUNT; i++) {
            result = lle_script_engine_init(
                &si->engines[i],
                (lle_script_engine_type_t)i,
                memory_pool,
                security_manager
            );
            if (result != LLE_SUCCESS) {
                lle_log_warn("Failed to initialize script engine %d", i);
                // Continue with other engines
            }
        }
    
        // Step 4: Initialize API registry
        result = lle_script_api_registry_init(&si->api_registry, memory_pool);
        if (result != LLE_SUCCESS) {
            lle_script_integration_destroy(si);
            return result;
        }
    
        // Step 5: Register core LLE APIs
        result = lle_script_integration_register_core_apis(si);
        if (result != LLE_SUCCESS) {
            lle_script_integration_destroy(si);
            return result;
        }
    
        return LLE_SUCCESS;
    }

    // Execute script function with comprehensive error handling
    lle_result_t lle_script_integration_execute_function(
        lle_script_integration_t *integration,
        const char *script_name,
        const char *function_name,
        lle_script_args_t *args,
        lle_script_result_t *result_out
    ) {
        // Step 1: Validate parameters
        if (!integration || !script_name || !function_name || !result_out) {
            return LLE_ERROR_INVALID_PARAMETER;
        }
    
        result_out->success = false;
        result_out->error_message = NULL;
    
        // Step 2: Look up loaded script
        lle_loaded_script_t *script = NULL;
        lle_result_t lookup_result = lle_hashtable_get(
            integration->loaded_scripts,
            script_name,
            (void**)&script
        );
        if (lookup_result != LLE_SUCCESS || !script) {
            result_out->error_message = "Script not loaded";
            integration->stats.script_not_found++;
            return LLE_ERROR_SCRIPT_NOT_FOUND;
        }
    
        // Step 3: Security check
        lle_result_t security_result = lle_security_manager_check_script_execution(
            integration->security_manager,
            script,
            function_name
        );
        if (security_result != LLE_SUCCESS) {
            result_out->error_message = "Script execution denied by security policy";
            integration->stats.security_violations++;
            return LLE_ERROR_SECURITY_VIOLATION;
        }
    
        // Step 4: Get appropriate script engine
        lle_script_engine_t *engine = integration->engines[script->engine_type];
        if (!engine) {
            result_out->error_message = "Script engine not available";
            integration->stats.engine_errors++;
            return LLE_ERROR_SCRIPT_ENGINE_UNAVAILABLE;
        }
    
        // Step 5: Performance monitoring setup
        uint64_t start_time = lle_get_timestamp_us();
        lle_performance_monitor_start_measurement(
            integration->performance_monitor,
            "script_execution"
        );
    
        // Step 6: Execute script function
        lle_result_t execution_result = LLE_SUCCESS;
        switch (script->engine_type) {
            case LLE_SCRIPT_ENGINE_LUA:
                execution_result = lle_script_lua_execute_function(
                    engine,
                    script,
                    function_name,
                    args,
                    result_out
                );
                break;
            
            case LLE_SCRIPT_ENGINE_PYTHON:
                execution_result = lle_script_python_execute_function(
                    engine,
                    script,
                    function_name,
                    args,
                    result_out
                );
                break;
            
            default:
                result_out->error_message = "Unsupported script engine";
                execution_result = LLE_ERROR_UNSUPPORTED_OPERATION;
                break;
        }
    
        // Step 7: Performance monitoring completion
        uint64_t end_time = lle_get_timestamp_us();
        uint64_t execution_time = end_time - start_time;
    
        lle_performance_monitor_end_measurement(
            integration->performance_monitor,
            "script_execution",
            execution_time
        );
    
        // Step 8: Update statistics
        integration->stats.total_executions++;
        integration->stats.total_execution_time += execution_time;
    
        if (execution_result == LLE_SUCCESS && result_out->success) {
            integration->stats.successful_executions++;
        } else {
            integration->stats.failed_executions++;
        }
    
        // Check execution time threshold
        if (execution_time > 1000) { // 1ms threshold
            integration->stats.slow_executions++;
            lle_log_warn("Slow script execution: %s::%s took %llu us", 
                         script_name, function_name, execution_time);
        }
    
        return execution_result;
    }
    ```

    ### **Lua Integration Implementation**

    ```c
    // Lua-specific script engine implementation
    lle_result_t lle_script_lua_execute_function(
        lle_script_engine_t *engine,
        lle_loaded_script_t *script,
        const char *function_name,
        lle_script_args_t *args,
        lle_script_result_t *result_out
    ) {
        lua_State *L = (lua_State*)engine->engine_state;
    
        // Step 1: Get function from global table
        lua_getglobal(L, function_name);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            result_out->error_message = "Function not found";
            return LLE_ERROR_SCRIPT_FUNCTION_NOT_FOUND;
        }
    
        // Step 2: Push arguments onto Lua stack
        int arg_count = 0;
        if (args && args->count > 0) {
            for (size_t i = 0; i < args->count; i++) {
                lle_result_t push_result = lle_script_lua_push_argument(L, &args->args[i]);
                if (push_result != LLE_SUCCESS) {
                    lua_pop(L, arg_count + 1); // Pop function and arguments
                    result_out->error_message = "Failed to push argument";
                    return push_result;
                }
                arg_count++;
            }
        }
    
        // Step 3: Call function with error handling
        int lua_result = lua_pcall(L, arg_count, LUA_MULTRET, 0);
        if (lua_result != LUA_OK) {
            // Handle Lua error
            const char *error_msg = lua_tostring(L, -1);
            result_out->error_message = lle_string_duplicate(
                error_msg ? error_msg : "Unknown Lua error",
                engine->memory_pool
            );
            lua_pop(L, 1); // Pop error message
            return LLE_ERROR_SCRIPT_EXECUTION_FAILED;
        }
    
        // Step 4: Process return values
        int return_count = lua_gettop(L);
        if (return_count > 0) {
            result_out->return_values = lle_memory_pool_alloc(
                engine->memory_pool,
                return_count * sizeof(lle_script_value_t)
            );
            if (!result_out->return_values) {
                lua_pop(L, return_count);
                return LLE_ERROR_OUT_OF_MEMORY;
            }
        
            result_out->return_count = return_count;
        
            for (int i = 0; i < return_count; i++) {
                int stack_index = -(return_count - i);
                lle_result_t convert_result = lle_script_lua_stack_to_value(
                    L,
                    stack_index,
                    &result_out->return_values[i],
                    engine->memory_pool
                );
                if (convert_result != LLE_SUCCESS) {
                    lua_pop(L, return_count);
                    return convert_result;
                }
            }
        
            lua_pop(L, return_count);
        }
    
        result_out->success = true;
        return LLE_SUCCESS;
    }

    // Register core LLE APIs for script access
    lle_result_t lle_script_integration_register_core_apis(lle_script_integration_t *integration) {
        lle_script_api_registry_t *registry = integration->api_registry;
    
        // Buffer manipulation APIs
        lle_script_api_function_t buffer_apis[] = {
            { "buffer_get_text", lle_script_api_buffer_get_text },
            { "buffer_set_text", lle_script_api_buffer_set_text },
            { "buffer_insert_text", lle_script_api_buffer_insert_text },
            { "buffer_delete_range", lle_script_api_buffer_delete_range },
            { "buffer_get_cursor_position", lle_script_api_buffer_get_cursor_position },
            { "buffer_set_cursor_position", lle_script_api_buffer_set_cursor_position },
            { "buffer_get_selection", lle_script_api_buffer_get_selection },
            { "buffer_set_selection", lle_script_api_buffer_set_selection },
        };
    
        // Display system APIs
        lle_script_api_function_t display_apis[] = {
            { "display_refresh", lle_script_api_display_refresh },
            { "display_set_prompt", lle_script_api_display_set_prompt },
            { "display_show_message", lle_script_api_display_show_message },
            { "display_clear_line", lle_script_api_display_clear_line },
        };
    
        // History system APIs
        lle_script_api_function_t history_apis[] = {
            { "history_search", lle_script_api_history_search },
            { "history_add_entry", lle_script_api_history_add_entry },
            { "history_get_recent", lle_script_api_history_get_recent },
        };
    
        // Completion system APIs
        lle_script_api_function_t completion_apis[] = {
            { "completion_get_candidates", lle_script_api_completion_get_candidates },
            { "completion_add_source", lle_script_api_completion_add_source },
        };
    
        // Key binding APIs
        lle_script_api_function_t keybinding_apis[] = {
            { "keybinding_create", lle_script_api_keybinding_create },
            { "keybinding_remove", lle_script_api_keybinding_remove },
            { "keybinding_list", lle_script_api_keybinding_list },
        };
    
        // Register all API categories
        lle_result_t result = lle_script_api_registry_register_category(
            registry, "buffer", buffer_apis, 
            sizeof(buffer_apis) / sizeof(buffer_apis[0])
        );
        if (result != LLE_SUCCESS) return result;
    
        result = lle_script_api_registry_register_category(
            registry, "display", display_apis,
            sizeof(display_apis) / sizeof(display_apis[0])
        );
        if (result != LLE_SUCCESS) return result;
    
        result = lle_script_api_registry_register_category(
            registry, "history", history_apis,
            sizeof(history_apis) / sizeof(history_apis[0])
        );
        if (result != LLE_SUCCESS) return result;
    
        result = lle_script_api_registry_register_category(
            registry, "completion", completion_apis,
            sizeof(completion_apis) / sizeof(completion_apis[0])
        );
        if (result != LLE_SUCCESS) return result;
    
        result = lle_script_api_registry_register_category(
            registry, "keybinding", keybinding_apis,
            sizeof(keybinding_apis) / sizeof(keybinding_apis[0])
        );
        if (result != LLE_SUCCESS) return result;
    
        return LLE_SUCCESS;
    }
    ```

    ---

    ## **WIDGET FRAMEWORK SYSTEM**

    The widget framework enables users to create custom editing operations and behaviors through a flexible, extensible architecture.

    ### **Widget Architecture**

    ```c
    typedef enum {
        LLE_WIDGET_TYPE_EDIT_OPERATION,  // Text editing operation
        LLE_WIDGET_TYPE_DISPLAY_ELEMENT, // Display element widget
        LLE_WIDGET_TYPE_INPUT_HANDLER,   // Input handling widget
        LLE_WIDGET_TYPE_COMPLETION_SOURCE, // Completion source widget
        LLE_WIDGET_TYPE_SYNTAX_HIGHLIGHTER, // Syntax highlighting widget
        LLE_WIDGET_TYPE_COMPOSITE        // Composite widget containing others
    } lle_widget_type_t;

    typedef struct lle_user_widget {
        // Widget identification
        char                        *name;
        char                        *description;
        lle_widget_type_t           type;
        char                        *version;
        char                        *author;
    
        // Implementation
        lle_widget_implementation_t implementation;
    
        // Dependencies
        char                        **dependencies;
        size_t                      dependency_count;
    
        // Configuration
        lle_hashtable_t             *config_schema;
        lle_hashtable_t             *current_config;
    
        // Lifecycle
        lle_widget_lifecycle_hooks_t hooks;
        lle_widget_state_t          state;
        bool                        enabled;
    
        // Performance
        lle_widget_performance_t    performance;
        uint64_t                    last_execution_time;
        uint32_t                    execution_count;
    
        // Security
        lle_widget_permissions_t    permissions;
        lle_security_context_t      *security_context;
    
        // Memory management
        lle_memory_pool_t           *memory_pool;
        size_t                      memory_usage;
    } lle_user_widget_t;

    typedef struct {
        // Widget registry
        lle_hashtable_t             *registered_widgets;
        lle_hashtable_t             *widget_metadata;
    
        // Widget categories
        lle_hashtable_t             *widgets_by_type;
        lle_hashtable_t             *widgets_by_category;
    
        // Execution context
        lle_widget_execution_context_t *execution_context;
        lle_widget_runtime_t        *runtime;
    
        // Integration
        lle_customization_system_t  *customization_system;
        lle_script_integration_t    *script_integration;
        lle_keybinding_manager_t    *keybinding_manager;
    
        // Security
        lle_security_manager_t      *security_manager;
        bool                        sandboxing_enabled;
    
        // Performance monitoring
        lle_performance_monitor_t   *performance_monitor;
        lle_widget_framework_stats_t stats;
    
        // Memory management
        lle_memory_pool_t           *memory_pool;
    } lle_widget_framework_t;
    ```

    ### **Widget Framework Implementation**

    ```c
    // Initialize widget framework
    lle_result_t lle_widget_framework_init(
        lle_widget_framework_t **framework,
        lle_memory_pool_t *memory_pool,
        lle_security_manager_t *security_manager,
        lle_customization_system_t *customization_system
    ) {
        // Step 1: Allocate framework structure
        *framework = lle_memory_pool_alloc(memory_pool, sizeof(lle_widget_framework_t));
        if (!*framework) {
            return LLE_ERROR_OUT_OF_MEMORY;
        }
    
        lle_widget_framework_t *wf = *framework;
        memset(wf, 0, sizeof(lle_widget_framework_t));
        wf->memory_pool = memory_pool;
        wf->security_manager = security_manager;
        wf->customization_system = customization_system;
        wf->sandboxing_enabled = true;
    
        // Step 2: Initialize widget storage
        lle_result_t result = lle_hashtable_create(
            &wf->registered_widgets,
            256,
            lle_hash_string,
            lle_compare_string,
            memory_pool
        );
        if (result != LLE_SUCCESS) {
            lle_widget_framework_destroy(wf);
            return result;
        }
    
        result = lle_hashtable_create(
            &wf->widget_metadata,
            256,
            lle_hash_string,
            lle_compare_string,
            memory_pool
        );
        if (result != LLE_SUCCESS) {
            lle_widget_framework_destroy(wf);
            return result;
        }
    
        result = lle_hashtable_create(
            &wf->widgets_by_type,
            16,
            lle_hash_int,
            lle_compare_int,
            memory_pool
        );
        if (result != LLE_SUCCESS) {
            lle_widget_framework_destroy(wf);
            return result;
        }
    
        result = lle_hashtable_create(
            &wf->widgets_by_category,
            64,
            lle_hash_string,
            lle_compare_string,
            memory_pool
        );
        if (result != LLE_SUCCESS) {
            lle_widget_framework_destroy(wf);
            return result;
        }
    
        // Step 3: Initialize execution context
        result = lle_widget_execution_context_init(&wf->execution_context, memory_pool);
        if (result != LLE_SUCCESS) {
            lle_widget_framework_destroy(wf);
            return result;
        }
    
        // Step 4: Initialize widget runtime
        result = lle_widget_runtime_init(&wf->runtime, memory_pool, security_manager);
        if (result != LLE_SUCCESS) {
            lle_widget_framework_destroy(wf);
            return result;
        }
    
        // Step 5: Register built-in widgets
        result = lle_widget_framework_register_builtin_widgets(wf);
        if (result != LLE_SUCCESS) {
            lle_widget_framework_destroy(wf);
            return result;
        }
    
        return LLE_SUCCESS;
    }

    // Register user-defined widget
    lle_result_t lle_widget_framework_register_widget(
        lle_widget_framework_t *framework,
        lle_user_widget_t *widget
    ) {
        // Step 1: Validate parameters
        if (!framework || !widget || !widget->name) {
            return LLE_ERROR_INVALID_PARAMETER;
        }

        // Step 2: Check for duplicate widget names
        lle_user_widget_t *existing_widget = NULL;
        if (lle_hashtable_get(framework->registered_widgets, widget->name, (void**)&existing_widget) == LLE_SUCCESS) {
            return LLE_ERROR_WIDGET_ALREADY_EXISTS;
        }

        // Step 3: Security validation
        lle_result_t security_result = lle_security_manager_validate_widget(
            framework->security_manager,
            widget
        );
        if (security_result != LLE_SUCCESS) {
            return security_result;
        }

        // Step 4: Validate dependencies
        for (size_t i = 0; i < widget->dependency_count; i++) {
            lle_user_widget_t *dependency = NULL;
            lle_result_t dep_result = lle_hashtable_get(
                framework->registered_widgets,
                widget->dependencies[i],
                (void**)&dependency
            );
            if (dep_result != LLE_SUCCESS || !dependency) {
                return LLE_ERROR_WIDGET_DEPENDENCY_MISSING;
            }
        }

        // Step 5: Initialize widget state
        widget->state = LLE_WIDGET_STATE_REGISTERED;
        widget->enabled = true;
        widget->execution_count = 0;
        widget->memory_usage = sizeof(lle_user_widget_t);

        // Step 6: Create security context
        lle_result_t context_result = lle_security_context_create_for_widget(
            &widget->security_context,
            widget,
            framework->memory_pool
        );
        if (context_result != LLE_SUCCESS) {
            return context_result;
        }

        // Step 7: Register widget in primary registry
        lle_result_t result = lle_hashtable_insert(
            framework->registered_widgets,
            widget->name,
            widget
        );
        if (result != LLE_SUCCESS) {
            lle_security_context_destroy(widget->security_context);
            return result;
        }

        // Step 8: Add to type-based categorization
        lle_widget_list_t *type_list = NULL;
        if (lle_hashtable_get(framework->widgets_by_type, &widget->type, (void**)&type_list) != LLE_SUCCESS) {
            result = lle_widget_list_create(&type_list, framework->memory_pool);
            if (result != LLE_SUCCESS) {
                lle_hashtable_remove(framework->registered_widgets, widget->name);
                lle_security_context_destroy(widget->security_context);
                return result;
            }
            lle_hashtable_insert(framework->widgets_by_type, &widget->type, type_list);
        }
        lle_widget_list_add(type_list, widget);

        // Step 9: Call initialization hook
        if (widget->hooks.on_register) {
            lle_widget_hook_result_t hook_result = widget->hooks.on_register(widget, framework->execution_context);
            if (hook_result != LLE_WIDGET_HOOK_SUCCESS) {
                lle_widget_framework_unregister_widget(framework, widget->name);
                return LLE_ERROR_WIDGET_INITIALIZATION_FAILED;
            }
        }

        // Step 10: Update statistics
        framework->stats.total_widgets++;
        framework->stats.widgets_by_type[widget->type]++;

        return LLE_SUCCESS;
    }

    // Execute widget with comprehensive error handling and security
    lle_result_t lle_widget_framework_execute_widget(
        lle_widget_framework_t *framework,
        const char *widget_name,
        lle_widget_execution_params_t *params,
        lle_widget_execution_result_t *result_out
    ) {
        // Step 1: Validate parameters
        if (!framework || !widget_name || !result_out) {
            return LLE_ERROR_INVALID_PARAMETER;
        }

        result_out->success = false;
        result_out->error_message = NULL;

        // Step 2: Look up widget
        lle_user_widget_t *widget = NULL;
        lle_result_t lookup_result = lle_hashtable_get(
            framework->registered_widgets,
            widget_name,
            (void**)&widget
        );
        if (lookup_result != LLE_SUCCESS || !widget) {
            result_out->error_message = "Widget not found";
            framework->stats.widget_not_found++;
            return LLE_ERROR_WIDGET_NOT_FOUND;
        }

        // Step 3: Check if widget is enabled
        if (!widget->enabled) {
            result_out->error_message = "Widget is disabled";
            framework->stats.widget_disabled++;
            return LLE_ERROR_WIDGET_DISABLED;
        }

        // Step 4: Security check
        lle_result_t security_result = lle_security_manager_check_widget_execution(
            framework->security_manager,
            widget,
            params
        );
        if (security_result != LLE_SUCCESS) {
            result_out->error_message = "Widget execution denied by security policy";
            framework->stats.security_violations++;
            return LLE_ERROR_SECURITY_VIOLATION;
        }

        // Step 5: Performance monitoring setup
        uint64_t start_time = lle_get_timestamp_us();
        lle_performance_monitor_start_measurement(
            framework->performance_monitor,
            "widget_execution"
        );

        // Step 6: Execute widget
        lle_result_t execution_result = lle_widget_runtime_execute(
            framework->runtime,
            widget,
            params,
            result_out
        );

        // Step 7: Performance monitoring completion
        uint64_t end_time = lle_get_timestamp_us();
        uint64_t execution_time = end_time - start_time;

        lle_performance_monitor_end_measurement(
            framework->performance_monitor,
            "widget_execution",
            execution_time
        );

        // Step 8: Update statistics
        widget->execution_count++;
        widget->last_execution_time = execution_time;
        framework->stats.total_executions++;
        framework->stats.total_execution_time += execution_time;

        if (execution_result == LLE_SUCCESS && result_out->success) {
            framework->stats.successful_executions++;
        } else {
            framework->stats.failed_executions++;
        }

        // Check execution time threshold
        if (execution_time > 500) { // 500us threshold
            framework->stats.slow_executions++;
            lle_log_warn("Slow widget execution: %s took %llu us", widget_name, execution_time);
        }

        return execution_result;
    }
    ```

    ---

    ## **SECURITY AND PERMISSIONS SYSTEM**

    ### **Security Architecture**

    The security system provides comprehensive protection for user customization through sandboxing, permission management, and audit logging.

    ```c
    typedef enum {
        LLE_PERMISSION_BUFFER_READ,
        LLE_PERMISSION_BUFFER_WRITE,
        LLE_PERMISSION_DISPLAY_CONTROL,
        LLE_PERMISSION_FILE_ACCESS,
        LLE_PERMISSION_NETWORK_ACCESS,
        LLE_PERMISSION_SYSTEM_COMMANDS,
        LLE_PERMISSION_CONFIGURATION_READ,
        LLE_PERMISSION_CONFIGURATION_WRITE,
        LLE_PERMISSION_PLUGIN_MANAGEMENT,
        LLE_PERMISSION_KEYBINDING_MODIFICATION,
        LLE_PERMISSION_COUNT
    } lle_permission_type_t;

    typedef struct {
        bool                        permissions[LLE_PERMISSION_COUNT];
        lle_hashtable_t             *resource_limits;      // Resource usage limits
        lle_hashtable_t             *allowed_paths;        // File system access restrictions
        lle_hashtable_t             *blocked_functions;    // Blocked API functions
        uint64_t                    execution_time_limit;  // Maximum execution time
        size_t                      memory_limit;          // Maximum memory usage
        bool                        network_enabled;       // Network access allowed
        bool                        file_system_enabled;   // File system access allowed
    } lle_security_permissions_t;

    typedef struct {
        // Sandboxing
        lle_sandbox_t               *sandbox;
        bool                        sandboxing_enabled;

        // Permission management
        lle_hashtable_t             *permission_profiles;
        lle_security_permissions_t  default_permissions;

        // Audit logging
        lle_audit_logger_t          *audit_logger;
        bool                        audit_enabled;

        // Security policies
        lle_hashtable_t             *security_policies;
        lle_security_policy_t       *active_policy;

        // Resource monitoring
        lle_resource_monitor_t      *resource_monitor;
        lle_security_stats_t        stats;

        // Memory management
        lle_memory_pool_t           *memory_pool;
    } lle_security_manager_t;
    ```

    ### **Security Implementation**

    ```c
    // Check security permissions for widget execution
    lle_result_t lle_security_manager_check_widget_execution(
        lle_security_manager_t *manager,
        lle_user_widget_t *widget,
        lle_widget_execution_params_t *params
    ) {
        // Step 1: Validate parameters
        if (!manager || !widget) {
            return LLE_ERROR_INVALID_PARAMETER;
        }

        // Step 2: Check if widget has security context
        if (!widget->security_context) {
            manager->stats.missing_security_context++;
            return LLE_ERROR_SECURITY_VIOLATION;
        }

        // Step 3: Get widget permissions
        lle_security_permissions_t *permissions = &widget->permissions;

        // Step 4: Check required permissions based on widget type
        switch (widget->type) {
            case LLE_WIDGET_TYPE_EDIT_OPERATION:
                if (!permissions->permissions[LLE_PERMISSION_BUFFER_WRITE]) {
                    manager->stats.permission_denied[LLE_PERMISSION_BUFFER_WRITE]++;
                    return LLE_ERROR_PERMISSION_DENIED;
                }
                break;

            case LLE_WIDGET_TYPE_DISPLAY_ELEMENT:
                if (!permissions->permissions[LLE_PERMISSION_DISPLAY_CONTROL]) {
                    manager->stats.permission_denied[LLE_PERMISSION_DISPLAY_CONTROL]++;
                    return LLE_ERROR_PERMISSION_DENIED;
                }
                break;

            case LLE_WIDGET_TYPE_INPUT_HANDLER:
                if (!permissions->permissions[LLE_PERMISSION_BUFFER_READ]) {
                    manager->stats.permission_denied[LLE_PERMISSION_BUFFER_READ]++;
                    return LLE_ERROR_PERMISSION_DENIED;
                }
                break;

            default:
                break;
        }

        // Step 5: Check resource limits
        if (widget->memory_usage > permissions->memory_limit) {
            manager->stats.resource_limit_exceeded++;
            return LLE_ERROR_RESOURCE_LIMIT_EXCEEDED;
        }

        if (widget->last_execution_time > permissions->execution_time_limit) {
            manager->stats.execution_time_exceeded++;
            return LLE_ERROR_EXECUTION_TIME_EXCEEDED;
        }

        // Step 6: Audit log the access attempt
        if (manager->audit_enabled) {
            lle_audit_entry_t entry = {
                .timestamp = lle_get_timestamp_us(),
                .event_type = LLE_AUDIT_WIDGET_EXECUTION,
                .widget_name = widget->name,
                .permissions_checked = true,
                .access_granted = true,
                .resource_usage = widget->memory_usage
            };
            lle_audit_logger_log(manager->audit_logger, &entry);
        }

        manager->stats.successful_permission_checks++;
        return LLE_SUCCESS;
    }

    // Validate widget security during registration
    lle_result_t lle_security_manager_validate_widget(
        lle_security_manager_t *manager,
        lle_user_widget_t *widget
    ) {
        // Step 1: Validate widget metadata
        if (!widget->name || strlen(widget->name) == 0) {
            return LLE_ERROR_INVALID_WIDGET_NAME;
        }

        if (!widget->version || strlen(widget->version) == 0) {
            return LLE_ERROR_INVALID_WIDGET_VERSION;
        }

        // Step 2: Check widget name against security blacklist
        if (lle_security_manager_is_widget_blacklisted(manager, widget->name)) {
            manager->stats.blacklisted_widgets++;
            return LLE_ERROR_WIDGET_BLACKLISTED;
        }

        // Step 3: Validate implementation functions
        if (!widget->implementation.execute) {
            return LLE_ERROR_INVALID_WIDGET_IMPLEMENTATION;
        }

        // Step 4: Check for required security hooks
        if (manager->sandboxing_enabled) {
            if (!widget->hooks.on_security_check) {
                return LLE_ERROR_MISSING_SECURITY_HOOK;
            }
        }

        // Step 5: Scan for potentially dangerous operations
        lle_result_t scan_result = lle_security_manager_scan_widget_implementation(
            manager,
            widget
        );
        if (scan_result != LLE_SUCCESS) {
            manager->stats.dangerous_widgets++;
            return scan_result;
        }

        manager->stats.validated_widgets++;
        return LLE_SUCCESS;
    }
    ```

    ---

    ## **PERFORMANCE OPTIMIZATION SYSTEM**

    ### **Performance Architecture**

    ```c
    typedef struct {
        // Cache management
        lle_hashtable_t             *customization_cache;
        uint64_t                    cache_version;
        size_t                      cache_hit_count;
        size_t                      cache_miss_count;
        float                       cache_hit_rate;

        // Performance metrics
        uint64_t                    total_operations;
        uint64_t                    total_execution_time;
        uint64_t                    average_execution_time;
        uint64_t                    max_execution_time;
        uint64_t                    min_execution_time;

        // Memory optimization
        lle_memory_pool_t           *dedicated_pool;
        size_t                      pool_size;
        size_t                      pool_used;
        size_t                      pool_peak_usage;

        // Optimization strategies
        bool                        lazy_loading_enabled;
        bool                        prefetching_enabled;
        bool                        compression_enabled;

        // Monitoring
        lle_performance_monitor_t   *monitor;
        uint64_t                    last_optimization_time;
    } lle_customization_performance_t;
    ```

    ### **Performance Implementation**

    ```c
    // Optimize customization system performance
    lle_result_t lle_customization_performance_optimize(
        lle_customization_system_t *system
    ) {
        lle_customization_performance_t *perf = &system->performance;

        // Step 1: Analyze current performance metrics
        perf->cache_hit_rate = (float)perf->cache_hit_count / 
                              (perf->cache_hit_count + perf->cache_miss_count);

        // Step 2: Optimize cache if hit rate is below target
        if (perf->cache_hit_rate < 0.75f) {
            lle_result_t cache_result = lle_customization_optimize_cache(system);
            if (cache_result != LLE_SUCCESS) {
                lle_log_warn("Cache optimization failed");
            }
        }

        // Step 3: Memory pool optimization
        if (perf->pool_used > perf->pool_size * 0.8) {
            lle_result_t pool_result = lle_memory_pool_optimize(perf->dedicated_pool);
            if (pool_result != LLE_SUCCESS) {
                lle_log_warn("Memory pool optimization failed");
            }
        }

        // Step 4: Enable prefetching for frequently accessed items
        if (perf->prefetching_enabled) {
            lle_customization_prefetch_frequent_items(system);
        }

        // Step 5: Update optimization timestamp
        perf->last_optimization_time = lle_get_timestamp_us();

        return LLE_SUCCESS;
    }
    ```

    ---

    ## **INTEGRATION AND TESTING FRAMEWORK**

    ### **Integration Testing**

    ```c
    // Comprehensive integration test suite
    lle_result_t lle_user_customization_run_integration_tests(
        lle_customization_system_t *system
    ) {
        lle_test_suite_t test_suite;
        lle_test_suite_init(&test_suite, "User Customization Integration");

        // Key binding tests
        lle_test_suite_add_test(&test_suite, "key_binding_registration", 
                               test_key_binding_registration);
        lle_test_suite_add_test(&test_suite, "key_binding_lookup_performance",
                               test_key_binding_lookup_performance);
        lle_test_suite_add_test(&test_suite, "key_chord_processing",
                               test_key_chord_processing);

        // Configuration tests
        lle_test_suite_add_test(&test_suite, "config_type_safety",
                               test_config_type_safety);
        lle_test_suite_add_test(&test_suite, "config_validation",
                               test_config_validation);
        lle_test_suite_add_test(&test_suite, "config_live_reload",
                               test_config_live_reload);

        // Script integration tests
        lle_test_suite_add_test(&test_suite, "lua_script_execution",
                               test_lua_script_execution);
        lle_test_suite_add_test(&test_suite, "python_script_execution",
                               test_python_script_execution);
        lle_test_suite_add_test(&test_suite, "script_security_sandbox",
                               test_script_security_sandbox);

        // Widget framework tests
        lle_test_suite_add_test(&test_suite, "widget_registration",
                               test_widget_registration);
        lle_test_suite_add_test(&test_suite, "widget_execution_performance",
                               test_widget_execution_performance);
        lle_test_suite_add_test(&test_suite, "widget_security_validation",
                               test_widget_security_validation);

        // Performance tests
        lle_test_suite_add_test(&test_suite, "cache_hit_rate_target",
                               test_cache_hit_rate_target);
        lle_test_suite_add_test(&test_suite, "execution_time_limits",
                               test_execution_time_limits);
        lle_test_suite_add_test(&test_suite, "memory_usage_limits",
                               test_memory_usage_limits);

        // Integration tests
        lle_test_suite_add_test(&test_suite, "full_customization_workflow",
                               test_full_customization_workflow);
        lle_test_suite_add_test(&test_suite, "multi_engine_coordination",
                               test_multi_engine_coordination);
        lle_test_suite_add_test(&test_suite, "security_policy_enforcement",
                               test_security_policy_enforcement);

        return lle_test_suite_run(&test_suite);
    }
    ```

    ---

    ## **PERFORMANCE TARGETS AND SUCCESS CRITERIA**

    ### **Performance Requirements**

    | Component | Target | Measurement |
    |-----------|--------|-------------|
    | Key Binding Lookup | <500us | Average response time |
    | Configuration Access | <250us | Cache hit scenario |
    | Script Execution | <1ms | Simple script functions |
    | Widget Execution | <500us | Built-in widgets |
    | Cache Hit Rate | >90% | Customization operations |
    | Memory Usage | <10MB | Peak system usage |

    ### **Success Criteria**

    OK **Comprehensive Customization**: Complete control over all aspects of LLE behavior  
    OK **Performance Excellence**: Sub-millisecond response times for all operations  
    OK **Security Excellence**: Enterprise-grade security with comprehensive sandboxing  
    OK **Integration Excellence**: Seamless integration with all LLE core systems  
    OK **Extensibility Excellence**: Unlimited customization through scripts and widgets  
    OK **Usability Excellence**: Intuitive configuration and management interfaces  

    ---

    ## **IMPLEMENTATION ROADMAP**

    ### **Phase 1: Integration Foundation (Weeks 1-2)**
    **Priority**: Critical integration architecture setup

    **Week 1: Keybinding Integration**
    1. **Keybinding Integration Architecture**
       - Implement `lle_keybinding_integration_t` structure
       - Create `lle_custom_keybinding_engine_t` system
       - Set up mode switching manager (Emacs/Vi)
       - **Dependencies**: 25_default_keybindings_complete.md
       - **Success Criteria**: Mode switching <20us

    2. **Custom Keybinding System**
       - Implement user keybinding parser and validator
       - Create keybinding override management
       - Build GNU Readline compatibility layer
       - **Testing**: Custom keybinding accuracy >95%

    **Week 2: Widget & Completion Integration**
    1. **Widget Customization Integration**
       - Implement `lle_widget_customization_t` structure
       - Create widget customization engine
       - Set up bottom-prompt customization
       - **Dependencies**: 24_advanced_prompt_widget_hooks_complete.md
       - **Success Criteria**: Widget customization <100us

    2. **Completion Customization Integration**
       - Implement `lle_completion_customization_t` structure
       - Create completion category customization
       - Build ranking algorithm customization
       - **Dependencies**: 23_interactive_completion_menu_complete.md
       - **Testing**: Completion customization works across all categories

    ### **Phase 2: Enhanced Customization Framework (Weeks 3-4)**
    **Priority**: Advanced customization capabilities with integration

    **Week 3: Cross-System Customization**
    1. **Unified Customization Engine**
       - Integrate all three customization systems
       - Implement cross-system configuration sharing
       - Create unified customization validation
       - **Integration**: All customizations work cohesively
       - **Performance**: Configuration updates <50us

    2. **Enhanced Script Integration**
       - Upgrade script engines with integration APIs
       - Create widget and completion scripting interfaces
       - Build keybinding scripting support
       - **Testing**: Script integration with all systems

    **Week 4: Performance Integration**
    1. **Integration Performance Optimization**
       - Optimize customization loading overhead <200us
       - Implement integration-specific caching
       - Create performance monitoring for customizations
       - **Performance**: Total integration overhead <200us

    2. **Integration Testing Framework**
       - Build cross-system customization tests
       - Create performance regression testing
       - Implement integration validation suite
       - **Success Criteria**: All integration tests pass

    ### **Phase 3: Core Framework (Original + Integration Enhancements)**
    - Enhanced key binding manager with integration support
    - Integrated configuration system with cross-system validation
    - Enhanced security framework with integration sandboxing
    - Memory pool integration with optimized customization pools

    ### **Phase 4: Script Integration (Enhanced)**
    - Lua engine integration with widget/completion APIs
    - Python engine integration with keybinding APIs
    - Enhanced API registry with integration support
    - Enhanced sandboxing system with cross-system security

    ### **Phase 5: Widget Framework (Enhanced)**
    - Enhanced widget runtime with customization integration
    - Integration-aware security validation system
    - Performance monitoring with integration metrics
    - Enhanced widget collection with integration examples

    ### **Phase 6: Optimization (Enhanced)**
    - Integration-aware performance optimization
    - Enhanced cache system with cross-system coordination
    - Integration resource limit enforcement
    - Comprehensive testing framework with integration validation

    ---

    ## **CONCLUSION**

    This User Customization System specification v2.0.0 provides a comprehensive foundation for implementing unlimited personalization capabilities in the Lush Line Editor with complete Phase 2 integration support. The system delivers enterprise-grade security, sub-millisecond performance, and seamless integration with all critical core systems while maintaining the highest standards of professional development.

    **Key Achievements**:
    - **Complete Integration Implementation**: Every component specified with full integration support for keybinding, widget, and completion customization
    - **Cross-System Coordination**: Seamless integration with default keybindings, widget hooks, and completion menu systems
    - **Enhanced Performance**: Sub-500us customization targets with <200us total integration overhead
    - **Security Excellence**: Comprehensive sandboxing and permission management across all integration systems
    - **Professional Standards**: Enterprise-grade security, performance, and reliability maintained throughout integration
    - **Extensibility Excellence**: Unlimited customization through scripts and widgets with full integration API support

    **Integration Dependencies Successfully Implemented**:
    - **25_default_keybindings_complete.md**: Complete keybinding customization with Emacs/Vi mode support
    - **24_advanced_prompt_widget_hooks_complete.md**: Full widget hook customization with bottom-prompt support
    - **23_interactive_completion_menu_complete.md**: Complete completion menu customization with category and ranking support

    The specification maintains Lush's commitment to professional development standards while providing the foundation for guaranteed implementation success in the comprehensive LLE specification project with full Phase 2 User Interface Integration support.

### **User Empowerment Through Customization**

The User Customization System transforms LLE from a static line editor into a fully programmable editing environment. Users can:

- **Create Personal Workflows**: Custom keybindings and editing operations tailored to individual needs
- **Extend Functionality**: Plugins that add completely new capabilities without modifying core LLE
- **Adapt to Projects**: Project-specific configurations and completion sources
- **Share Configurations**: Export/import systems for team collaboration and personal backup
- **Script Integration**: Full access to LLE APIs through Lua and Python for advanced customization
- **Performance Optimization**: Monitor and optimize custom configurations for maximum efficiency

The system follows the principle that **users should never be limited by the original design** - if something can be customized or extended, the User Customization System provides a way to do it safely and efficiently.

    ---

    ## **THEME COMMAND INTEGRATION**

    ### **Visual Control Interface**

    The User Customization System provides complete visual control through the existing `theme` builtin command, enabling users to customize all visual aspects of LLE through the established theme system interface.

    #### **Theme Command Extensions**

    ```c
    // Theme command LLE integration
    typedef struct lle_theme_command_integration {
        // Command handlers
        lle_theme_command_handler_t     *command_handler;
        lle_theme_help_provider_t       *help_provider;
        lle_theme_completion_engine_t   *completion_engine;
        
        // Integration with existing theme system
        theme_manager_t                 *lush_theme_manager;
        lle_customization_system_t      *customization_system;
        
        // Visual customization components
        lle_color_scheme_manager_t      *color_scheme_manager;
        lle_syntax_theme_manager_t      *syntax_theme_manager;
        lle_visual_style_manager_t      *visual_style_manager;
        
    } lle_theme_command_integration_t;

    // Theme command interface implementation
    int lle_theme_command_handler(int argc, char **argv) {
        // Handle: theme lle <command> [options...]
        if (argc < 3) {
            lle_theme_show_command_help();
            return 1;
        }
        
        const char *lle_command = argv[2];
        
        // Color management commands
        if (strcmp(lle_command, "colors") == 0) {
            return lle_handle_theme_colors_command(argc - 3, argv + 3);
        } else if (strcmp(lle_command, "syntax") == 0) {
            return lle_handle_theme_syntax_command(argc - 3, argv + 3);
        }
        
        // Visual styling commands
        else if (strcmp(lle_command, "autosuggestions") == 0) {
            return lle_handle_theme_autosuggestions_command(argc - 3, argv + 3);
        } else if (strcmp(lle_command, "cursor") == 0) {
            return lle_handle_theme_cursor_command(argc - 3, argv + 3);
        } else if (strcmp(lle_command, "selection") == 0) {
            return lle_handle_theme_selection_command(argc - 3, argv + 3);
        }
        
        // Theme integration commands
        else if (strcmp(lle_command, "status") == 0) {
            return lle_handle_theme_status_command(argc - 3, argv + 3);
        } else if (strcmp(lle_command, "sync") == 0) {
            return lle_handle_theme_sync_command(argc - 3, argv + 3);
        }
        
        else {
            fprintf(stderr, "theme lle: unknown command '%s'\n", lle_command);
            lle_theme_show_command_help();
            return 1;
        }
    }
    ```

    #### **Color Scheme Management Integration**

    ```c
    // LLE color scheme management through theme command
    int lle_handle_theme_colors_command(int argc, char **argv) {
        if (argc < 1) {
            printf("LLE Color Commands:\n");
            printf("  theme lle colors show [--format=table|json]    Show current colors\n");
            printf("  theme lle colors list                         List available schemes\n");
            printf("  theme lle colors set <scheme>                 Set color scheme\n");
            printf("  theme lle colors reset                        Reset to theme defaults\n");
            return 0;
        }
        
        const char *colors_command = argv[0];
        
        if (strcmp(colors_command, "show") == 0) {
            return lle_show_current_color_scheme(argc - 1, argv + 1);
        } else if (strcmp(colors_command, "list") == 0) {
            return lle_list_available_color_schemes();
        } else if (strcmp(colors_command, "set") == 0) {
            if (argc < 2) {
                fprintf(stderr, "theme lle colors set: scheme name required\n");
                return 1;
            }
            return lle_set_color_scheme(argv[1]);
        } else if (strcmp(colors_command, "reset") == 0) {
            return lle_reset_color_scheme_to_theme_defaults();
        } else {
            fprintf(stderr, "theme lle colors: unknown command '%s'\n", colors_command);
            return 1;
        }
    }

    // Show current LLE color scheme
    int lle_show_current_color_scheme(int argc, char **argv) {
        const char *format = "table";
        bool verbose = false;
        
        // Parse options
        for (int i = 0; i < argc; i++) {
            if (strncmp(argv[i], "--format=", 9) == 0) {
                format = argv[i] + 9;
            } else if (strcmp(argv[i], "--verbose") == 0) {
                verbose = true;
            }
        }
        
        lle_color_scheme_t *current_scheme = lle_get_current_color_scheme();
        if (!current_scheme) {
            fprintf(stderr, "theme lle colors show: no active color scheme\n");
            return 1;
        }
        
        printf("Current LLE Color Scheme: %s\n", current_scheme->name);
        
        // Get active theme information
        theme_definition_t *active_theme = theme_get_active();
        if (active_theme) {
            printf("Based on theme: %s\n", active_theme->name);
        }
        
        printf("Last updated: %s\n", lle_format_timestamp(current_scheme->last_modified));
        
        if (strcmp(format, "table") == 0) {
            lle_display_color_table(current_scheme, verbose);
        } else if (strcmp(format, "json") == 0) {
            lle_export_color_scheme_json(current_scheme, stdout);
        } else {
            fprintf(stderr, "theme lle colors show: invalid format '%s'\n", format);
            return 1;
        }
        
        return 0;
    }
    ```

    #### **Syntax Theme Customization Integration**

    ```c
    // Syntax highlighting theme management through theme command
    int lle_handle_theme_syntax_command(int argc, char **argv) {
        if (argc < 1) {
            printf("LLE Syntax Theme Commands:\n");
            printf("  theme lle syntax colors                       Show syntax colors\n");
            printf("  theme lle syntax customize                    Customize syntax colors\n");
            printf("  theme lle syntax preview [file]               Preview syntax highlighting\n");
            printf("  theme lle syntax reset                        Reset to defaults\n");
            return 0;
        }
        
        const char *syntax_command = argv[0];
        
        if (strcmp(syntax_command, "colors") == 0) {
            return lle_show_syntax_highlighting_colors();
        } else if (strcmp(syntax_command, "customize") == 0) {
            return lle_interactive_syntax_customization();
        } else if (strcmp(syntax_command, "preview") == 0) {
            const char *file = (argc > 1) ? argv[1] : NULL;
            return lle_preview_syntax_highlighting(file);
        } else if (strcmp(syntax_command, "reset") == 0) {
            return lle_reset_syntax_theme_to_defaults();
        } else {
            fprintf(stderr, "theme lle syntax: unknown command '%s'\n", syntax_command);
            return 1;
        }
    }

    // Interactive syntax color customization
    int lle_interactive_syntax_customization(void) {
        printf("Interactive Syntax Color Customization\n");
        printf("======================================\n\n");
        
        lle_color_scheme_t *current_scheme = lle_get_current_color_scheme();
        if (!current_scheme) {
            fprintf(stderr, "No active color scheme available\n");
            return 1;
        }
        
        printf("Current syntax theme based on: %s\n", current_scheme->name);
        printf("Use Ctrl+C to exit at any time\n\n");
        
        // Interactive customization loop
        lle_syntax_element_t elements[] = {
            {"Command", &current_scheme->command_color},
            {"Builtin", &current_scheme->builtin_color},
            {"Keyword", &current_scheme->keyword_color},
            {"String", &current_scheme->string_color},
            {"Variable", &current_scheme->variable_color},
            {"Comment", &current_scheme->comment_color},
            {"Error", &current_scheme->error_color},
            {NULL, NULL}
        };
        
        for (int i = 0; elements[i].name; i++) {
            printf("Customize %s color (current: ", elements[i].name);
            lle_print_color_sample(elements[i].color, elements[i].name);
            printf(")\n");
            
            char input[256];
            printf("Enter new color (hex #RRGGBB, color name, or 'skip'): ");
            if (fgets(input, sizeof(input), stdin)) {
                input[strcspn(input, "\n")] = 0; // Remove newline
                
                if (strcmp(input, "skip") == 0) {
                    continue;
                }
                
                lle_color_t new_color;
                if (lle_parse_color_specification(input, &new_color) == LLE_SUCCESS) {
                    *elements[i].color = new_color;
                    printf("Updated %s color: ", elements[i].name);
                    lle_print_color_sample(&new_color, elements[i].name);
                    printf("\n");
                } else {
                    printf("Invalid color specification: %s\n", input);
                }
            }
            printf("\n");
        }
        
        // Save customizations
        lle_result_t result = lle_save_custom_color_scheme(current_scheme);
        if (result == LLE_SUCCESS) {
            printf("Syntax color customizations saved successfully\n");
            printf("Use 'theme lle syntax preview' to see the results\n");
        } else {
            fprintf(stderr, "Failed to save customizations: %s\n", lle_result_get_message(result));
            return 1;
        }
        
        return 0;
    }
    ```

    #### **Theme Command Help System**

    ```c
    // Theme command help integration
    void lle_theme_show_command_help(void) {
        printf("Lush Line Editor (LLE) Theme Commands\n\n");
        
        printf("Color Management:\n");
        printf("  theme lle colors show [--format=table|json]    Show current LLE colors\n");
        printf("  theme lle colors list                         List available color schemes\n");
        printf("  theme lle colors set <scheme>                 Set LLE color scheme\n");
        printf("  theme lle colors reset                        Reset to theme defaults\n");
        printf("  theme lle colors export <file>                Export custom colors\n");
        printf("  theme lle colors import <file>                Import custom colors\n\n");
        
        printf("Syntax Highlighting:\n");
        printf("  theme lle syntax colors                       Show syntax highlighting colors\n");
        printf("  theme lle syntax customize                    Customize syntax colors interactively\n");
        printf("  theme lle syntax preview [file]               Preview syntax highlighting\n");
        printf("  theme lle syntax reset                        Reset syntax colors to defaults\n\n");
        
        printf("Visual Styling:\n");
        printf("  theme lle autosuggestions style               Configure autosuggestion appearance\n");
        printf("  theme lle cursor style                        Configure cursor styling\n");
        printf("  theme lle selection style                     Configure selection highlighting\n");
        printf("  theme lle completion style                    Configure completion menu styling\n\n");
        
        printf("Theme Integration:\n");
        printf("  theme lle status                              Show LLE theme integration status\n");
        printf("  theme lle sync                               Synchronize LLE with current theme\n");
        printf("  theme lle preview                            Preview all LLE features with current theme\n\n");
        
        printf("Examples:\n");
        printf("  theme set dark && theme lle sync              Apply dark theme to LLE\n");
        printf("  theme lle colors set vibrant                  Use vibrant color scheme\n");
        printf("  theme lle syntax customize                    Interactively customize syntax colors\n");
        printf("  theme lle colors export my-colors.json        Export custom color scheme\n");
        printf("  theme lle autosuggestions style               Configure suggestion appearance\n\n");
        
        printf("Note: LLE automatically inherits colors from the active theme.\n");
        printf("      Use 'theme set <name>' to change the base theme for all components.\n");
        printf("      LLE-specific customizations override theme defaults.\n");
    }
    ```

    ### **Integration Architecture**

    The theme command integration follows these design principles:

    #### **Visual Domain Separation**
    - Theme command handles all visual and styling aspects of LLE
    - Integrates seamlessly with existing Lush theme system
    - Provides unified visual control for all LLE components
    - Maintains automatic synchronization with theme changes

    #### **Customization Hierarchy**
    - Base theme provides default colors and styles
    - LLE-specific customizations override theme defaults
    - User customizations take highest priority
    - Fallback mechanisms ensure visual consistency

    #### **Professional Interface Design**
    - Consistent with existing theme command patterns
    - Interactive customization tools for complex configurations
    - Export/import capabilities for sharing customizations
    - Real-time preview and validation of changes

    This integration ensures that LLE visual customization follows the established, professional interface patterns while providing comprehensive control over all visual aspects of the line editor experience.

    ---

    **Document Status**: OK **INTEGRATION-READY** (Phase 2 Integration Complete)
    **Phase 2 Status**: OK **COMPLETE** - All 3 Phase 2 specifications successfully refactored  
    **Next Priority**: Phase 3 Enhancement Integration - Performance Optimization, Memory Management, Error Handling
    **LLE Integration Progress**: 5/5 Integration Specifications Complete (100% success rate)