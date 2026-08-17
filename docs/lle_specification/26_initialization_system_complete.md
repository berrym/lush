# Spec 26: LLE Initialization System

**Version**: 1.0.0  
**Date**: 2025-12-26  
**Status**: SPECIFICATION  
**Classification**: Core Architecture - Shell Integration  
**Priority**: CRITICAL - Architectural Blocker  

## Executive Summary

This specification defines the LLE Initialization System, which provides clean, safe, and reversible integration of the Lush Line Editor (LLE) into the shell lifecycle. The system enables:

1. **Dual-Mode Operation**: Seamless coexistence of GNU readline and LLE
2. **Safe Fallback**: Ctrl+G recovery mechanism even when LLE is in corrupted state
3. **Shell-Level Events**: Directory change, pre/post-command hooks for prompt/theme integration
4. **Zero-Regression Design**: GNU readline functionality must never be hindered
5. **Graceful Degradation**: LLE failures must not crash the shell

This specification addresses the architectural blocker preventing the new LLE prompt/theme system (Spec 25) from being tested and integrated.

## Table of Contents

1. [Problem Statement](#1-problem-statement)
2. [Design Principles](#2-design-principles)
3. [Architecture Overview](#3-architecture-overview)
4. [Initialization Lifecycle](#4-initialization-lifecycle)
5. [Component Specifications](#5-component-specifications)
6. [Shell Event System](#6-shell-event-system)
7. [Safety Mechanisms](#7-safety-mechanisms)
8. [GNU Readline Coexistence](#8-gnu-readline-coexistence)
9. [Build Configuration](#9-build-configuration)
10. [Error Handling](#10-error-handling)
11. [Testing Requirements](#11-testing-requirements)
12. [Implementation Phases](#12-implementation-phases)
13. [API Reference](#13-api-reference)

---

## 1. Problem Statement

### 1.1 Current State Analysis

The current LLE implementation has a critical architectural gap:

**Problem**: LLE components are initialized lazily inside `lle_readline()` on first call:
```c
// Current: lle_readline.c line 2483-2512
if (!global_lle_editor) {
    result = lle_editor_create(&global_lle_editor, global_memory_pool);
    // ... history initialization ...
}
```

**Issues with Current Approach**:

1. **No Shell-Level Persistence**: The `global_lle_editor` is created lazily but there's no formal lifecycle management. Shell events cannot reach LLE components.

2. **Missing Event Integration**: Shell lifecycle events (`LLE_EVENT_DIRECTORY_CHANGED`, `LLE_EVENT_PRE_COMMAND`, `LLE_EVENT_POST_COMMAND`) are defined in Spec 04 but not wired to shell builtins.

3. **No Recovery Path**: If LLE enters a bad state, there's no guaranteed way to recover to GNU readline without restarting the shell.

4. **Prompt System Orphaned**: Spec 25 (Prompt/Theme System) exists but cannot be tested because:
   - Shell events don't reach the prompt composer
   - Per-session vs persistent components aren't properly separated
   - No integration hooks in shell initialization

5. **Build Option Mismatch**: Meson allows building without readline (`-Dreadline=disabled`), but code paths assume readline is always available as fallback.

### 1.2 Previous Attempts

Previous attempts at LLE initialization were "catastrophic" due to:
- Trying to replace too much at once
- Not maintaining GNU readline as a working fallback
- Complex state management causing cascading failures
- Missing safety mechanisms for recovery

### 1.3 Requirements

This specification must:

1. Define a safe, incremental initialization system
2. Maintain GNU readline as the default and always-functional fallback
3. Provide Ctrl+G recovery even in corrupted LLE state
4. Enable shell-level events to reach LLE components
5. Support meson build options (with/without readline)
6. Allow testing of Spec 25 components in isolation

---

## 2. Design Principles

### 2.1 Safety First

**Principle**: LLE failures must never prevent shell operation.

```c
// WRONG: Fatal error on LLE init failure
if (lle_init() != LLE_SUCCESS) {
    error_abort("LLE initialization failed");  // NEVER DO THIS
}
```

**CORRECT: Build-aware graceful recovery**

```c
lle_result_t result = lle_init();

if (result != LLE_SUCCESS) {
#ifdef HAVE_READLINE
    // Build includes GNU readline - can fall back
    if (config.use_lle) {
        config.use_lle = false;
        log_warning("LLE init failed, using GNU readline");
        // User can re-enable later via: display lle enable
    }
    // If already using readline, nothing to do
#else
    // LLE-only build - must self-recover
    log_warning("LLE init failed, attempting hard reset");
    lle_hard_reset();
    
    result = lle_init();
    if (result != LLE_SUCCESS) {
        // Try minimal mode (basic input, no advanced features)
        log_warning("LLE running in degraded mode");
        lle_init_minimal();
    }
#endif
}
```

**Three build scenarios:**

| Build | On LLE failure | Recovery path |
|-------|---------------|---------------|
| readline + LLE (default readline) | Switch to readline | User can re-enable LLE via `display lle enable` |
| readline + LLE (default LLE) | Switch to readline | User can retry LLE via `display lle enable` |
| LLE only | Hard reset, then minimal mode | User can retry via `lle reset` |

**Runtime switching**: The `display lle enable/disable` commands provide instant switching between readline and LLE at the next prompt. No shell restart required.

### 2.2 Incremental Integration

**Principle**: Initialize components in dependency order with validation at each step.

All steps are **shell-level** (persist for shell lifetime) unless noted:

```
Step 1: Memory pool -> validate        [Shell-level, already done in init()]
Step 2: Terminal detection -> validate [Shell-level, already done in init()]
Step 3: Shell event hub -> validate    [Shell-level, NEW - for cd/command events]
Step 4: Editor core -> validate        [Shell-level, global_lle_editor]
Step 5: History system -> validate     [Shell-level, part of editor]
Step 6: Prompt system -> validate      [Shell-level, Spec 25 composer]
Step 7: Shell event hooks -> validate  [Shell-level, wire builtins to hub]

Per-readline components (created/destroyed each lle_readline() call):
- lle_buffer_t (fresh buffer)
- lle_terminal_abstraction_t (raw mode handling)  
- lle_event_system_t (keystroke events)
- readline_context_t (session state)
```

### 2.3 Reversibility

**Principle**: Any initialization step must be reversible.

```c
typedef struct {
    bool memory_pool_initialized;
    bool terminal_initialized;
    bool event_system_initialized;
    bool editor_initialized;
    bool history_initialized;
    bool prompt_initialized;
    bool shell_hooks_installed;
} lle_init_state_t;

// Rollback on failure
void lle_init_rollback(lle_init_state_t *state) {
    if (state->shell_hooks_installed) lle_unhook_shell_events();
    if (state->prompt_initialized) lle_prompt_shutdown();
    if (state->history_initialized) lle_history_shutdown();
    if (state->editor_initialized) lle_editor_destroy(global_lle_editor);
    // ... etc
}
```

### 2.4 Separation of Concerns

**Principle**: Clear boundaries between:
- **Shell-level components** (persist across readline calls)
- **Readline-level components** (created per readline() invocation)
- **Display components** (managed by display integration)

---

## 3. Architecture Overview

### 3.1 Component Hierarchy

```
+---------------------------------------------------------------------+
|                         SHELL LEVEL                                  |
|   Initialized once in init() - persists for shell lifetime          |
+---------------------------------------------------------------------+
|  lle_shell_integration_t                                            |
|  +-- lle_init_state_t          (initialization tracking)           |
|  +-- lle_shell_event_hub_t     (shell->LLE event routing)           |
|  +-- lle_editor_t*             (global editor - persistent)         |
|  |   +-- history_system        (persistent across readline)         |
|  |   +-- kill_ring             (persistent across readline)         |
|  |   +-- widget_registry       (persistent across readline)         |
|  |   +-- keybinding_manager    (persistent across readline) [NOTE]  |
|  +-- lle_prompt_composer_t*    (Spec 25 - persistent)               |
|  |   +-- segment_registry      (persistent)                         |
|  |   +-- theme_registry        (persistent)                         |
|  |   +-- prompt_context        (updated by shell events)            |
|  +-- lle_terminal_caps_t*      (detected once - persistent)         |
+---------------------------------------------------------------------+
|                        READLINE LEVEL                                |
|   Created/destroyed per lle_readline() call                         |
+---------------------------------------------------------------------+
|  readline_context_t            (per-readline session)               |
|  +-- lle_buffer_t*             (fresh buffer per readline)          |
|  +-- lle_terminal_abstraction* (raw mode per readline)              |
|  +-- lle_event_system_t*       (keystroke events per readline)      |
|  +-- continuation_state_t      (multiline parser state)             |
|  +-- current_suggestion        (autosuggestion buffer)              |
+---------------------------------------------------------------------+
|                        DISPLAY LEVEL                                 |
|   Managed by display_integration (separate lifecycle)               |
+---------------------------------------------------------------------+
|  lle_display_integration_t     (Spec 08)                            |
|  +-- render_controller                                               |
|  +-- display_bridge                                                  |
|  +-- dirty_tracker                                                   |
+---------------------------------------------------------------------+
```

> **[NOTE] keybinding_manager persistence**: Currently in `lle_readline.c`, the 
> keybinding manager is created per-readline call. This spec moves it to shell-level
> as part of `lle_editor_t`. Rationale: User-set custom bindings must persist across
> commands for the duration of the shell session. This is consistent with how
> history, kill ring, and widget registry behave. The keybinding manager does not
> need to respond to shell events (cd, pre/post-command) - it only processes
> keystroke events during readline.

### 3.2 Initialization Flow

```
main() 
  v
init(argc, argv, &in)
  +-- [existing init steps 1-35]
  +-- Step 36: lle_shell_integration_init()  <- NEW
  |   +-- Check config.use_lle
  |   +-- If false: skip (GNU readline mode, LLE available via display cmd)
  |   +-- If true: initialize LLE components
  |   |   +-- lle_init_shell_event_hub()  <- NEW: shell event routing
  |   |   +-- lle_init_editor()           <- Includes keybinding_manager
  |   |   +-- lle_init_history()
  |   |   +-- lle_init_prompt_system()    <- Enables Spec 25
  |   |   +-- lle_install_shell_hooks()   <- Wire builtins to event hub
  |   +-- On failure: (see below)
  +-- [continue existing init]
  +-- return
  v
Main Loop
  +-- get_unified_input() -> lle_readline() or readline()
  +-- lle_fire_pre_command()  <- NEW: before execution
  +-- parse_and_execute()
  |   +-- [cd builtin] -> lle_fire_directory_changed()
  +-- lle_fire_post_command()  <- NEW: after execution (exit code, duration)
  +-- display_integration_post_command_update()
  v
Shell Exit
  +-- lle_shell_integration_shutdown()  <- NEW (via atexit)
```

**On initialization failure:**

```
#ifdef HAVE_READLINE
  +-- Set config.use_lle = false (fall back to GNU readline)
      User can retry later via: display lle enable
#else
  +-- lle_hard_reset() then retry
      If still failing: lle_init_minimal() (degraded mode)
#endif
```

### 3.3 Mode Selection

```c
// In lush_readline_with_prompt() - UNCHANGED LOGIC
char *lush_readline_with_prompt(const char *prompt) {
    if (config.use_lle) {
        return lle_readline(actual_prompt);
    } else {
        return readline(actual_prompt);
    }
}
```

The mode selection remains unchanged. The initialization system only affects:
1. Whether LLE components are pre-initialized at shell startup
2. Whether shell events are routed to LLE
3. Whether Spec 25 prompt system is active

---

## 4. Initialization Lifecycle

### 4.1 Shell Startup Sequence

**Dependency Order** (must be initialized in this exact order):

| Step | Component | Depends On | Fatal if Fails? |
|------|-----------|------------|-----------------|
| 1 | Memory pool | Nothing | Yes |
| 2 | Terminal detection | Memory pool | Yes |
| 3 | Shell event hub | Memory pool | Yes |
| 4 | Editor core | Memory pool, terminal | Yes |
| 5 | History system | Editor | No (continue without) |
| 6 | Prompt system | Editor, event hub | No (use fallback) |
| 7 | Shell event hooks | Event hub, builtins | No (prompts static) |

```c
/**
 * LLE Shell Integration Initialization
 * Called from init() after all prerequisite systems are ready
 */
lle_result_t lle_shell_integration_init(void) {
    lle_init_state_t state = {0};
    lle_result_t result;
    
    // Phase 1: Check prerequisites
    if (!IS_INTERACTIVE_SHELL) {
        return LLE_SUCCESS;  // LLE only needed for interactive shells
    }
    
    if (!config.use_lle) {
        return LLE_SUCCESS;  // User wants GNU readline (can enable later)
    }
    
    // Phase 2: Initialize persistent components in dependency order
    
    // Step 1: Memory pool
    // Verify global_memory_pool exists (initialized earlier in init())
    if (!global_memory_pool) {
        log_error("LLE: Memory pool not available");
        goto handle_failure;
    }
    state.memory_pool_initialized = true;
    
    // Step 2: Terminal detection
    // Verify terminal capabilities were detected (done earlier in init())
    if (!lle_get_cached_terminal_detection()) {
        log_error("LLE: Terminal detection not available");
        goto handle_failure;
    }
    state.terminal_initialized = true;
    
    // Step 3: Shell event hub (NEW - for cd/pre/post command events)
    result = lle_shell_event_hub_create(&g_shell_event_hub, global_memory_pool);
    if (result != LLE_SUCCESS) {
        log_error("LLE: Shell event hub creation failed: %s", 
                  lle_error_string(result));
        goto handle_failure;
    }
    state.event_hub_initialized = true;
    
    // Step 4: Editor core (includes keybinding_manager as persistent component)
    result = lle_editor_create(&global_lle_editor, global_memory_pool);
    if (result != LLE_SUCCESS) {
        log_error("LLE: Editor creation failed: %s", lle_error_string(result));
        goto handle_failure;
    }
    state.editor_initialized = true;
    
    // Step 5: History system (non-fatal if fails)
    lle_history_config_t hist_config;
    populate_history_config_from_lush_config(&hist_config);
    result = lle_history_core_create(&global_lle_editor->history_system,
                                     global_lle_editor->lle_pool,
                                     &hist_config);
    if (result != LLE_SUCCESS) {
        log_warning("LLE: History creation failed, continuing without history");
        // Non-fatal: continue without history
    } else {
        state.history_initialized = true;
        // Load existing history from file
        lle_history_load_from_file(global_lle_editor->history_system,
                                   get_lle_history_path());
    }
    
    // Step 6: Prompt system - Spec 25 (non-fatal if fails)
    result = lle_prompt_system_init(&global_prompt_composer, g_shell_event_hub);
    if (result != LLE_SUCCESS) {
        log_warning("LLE: Prompt system failed, using fallback prompts");
        // Non-fatal: use src/prompt.c fallback
    } else {
        state.prompt_initialized = true;
    }
    
    // Step 7: Shell event hooks - wire builtins to event hub (non-fatal if fails)
    result = lle_install_shell_event_hooks(g_shell_event_hub);
    if (result != LLE_SUCCESS) {
        log_warning("LLE: Shell hooks failed, prompts won't update dynamically");
        // Non-fatal: prompts work but don't update on cd, etc.
    } else {
        state.shell_hooks_installed = true;
    }
    
    // Phase 3: Store state for later shutdown
    g_lle_init_state = state;
    
    return LLE_SUCCESS;
    
handle_failure:
    // Rollback whatever was initialized
    lle_init_rollback(&state);
    
#ifdef HAVE_READLINE
    // Build includes GNU readline - fall back to it
    config.use_lle = false;
    log_info("LLE init failed, using GNU readline");
    log_info("Re-enable with: display lle enable");
    return LLE_SUCCESS;  // Fallback is success
#else
    // LLE-only build - must self-recover
    log_warning("LLE init failed, attempting hard reset");
    lle_hard_reset();
    
    // Retry initialization once
    memset(&state, 0, sizeof(state));
    // ... simplified retry of critical steps only ...
    
    if (/* retry also failed */) {
        log_warning("LLE running in degraded mode (basic input only)");
        return lle_init_minimal();
    }
    return LLE_SUCCESS;
#endif
}
```

### 4.2 Shell Shutdown Sequence

```c
/**
 * LLE Shell Integration Shutdown
 * Registered via atexit() in init()
 */
void lle_shell_integration_shutdown(void) {
    // Save history before shutdown
    if (g_lle_init_state.history_initialized && global_lle_editor &&
        global_lle_editor->history_system) {
        lle_history_save_to_file(global_lle_editor->history_system,
                                 get_lle_history_path());
    }
    
    // Rollback all initialized components
    lle_init_rollback(&g_lle_init_state);
    
    // Clear global state
    global_lle_editor = NULL;
    global_prompt_composer = NULL;
    memset(&g_lle_init_state, 0, sizeof(g_lle_init_state));
}
```

### 4.3 Per-Readline Lifecycle

Each `lle_readline()` call creates fresh per-session components while reusing
persistent shell-level components from `global_lle_editor`.

**Component ownership:**

| Component | Created | Destroyed | Notes |
|-----------|---------|-----------|-------|
| buffer | Per-readline | Per-readline | Fresh for each command |
| terminal_abstraction | Per-readline | Per-readline | Raw mode per session |
| event_system | Per-readline | Per-readline | Keystroke events only |
| continuation_state | Per-readline | Per-readline | Multiline parser |
| current_suggestion | Per-readline | Per-readline | Autosuggestion buffer |
| keybinding_manager | Shell init | Shell shutdown | **Persistent** (from editor) |
| history_system | Shell init | Shell shutdown | **Persistent** (from editor) |
| kill_ring | Shell init | Shell shutdown | **Persistent** (from editor) |

```c
char *lle_readline(const char *prompt) {
    // Step 1: Create per-readline components only
    lle_buffer_t *buffer = NULL;
    lle_buffer_create(&buffer, global_memory_pool, 0);
    
    lle_terminal_abstraction_t *term = NULL;
    lle_terminal_abstraction_create(&term);
    
    lle_event_system_t *event_system = NULL;
    lle_event_system_create(&event_system);
    
    continuation_state_t continuation_state;
    continuation_state_init(&continuation_state);
    
    // Step 2: Attach fresh buffer to persistent editor
    if (global_lle_editor) {
        global_lle_editor->buffer = buffer;
        global_lle_editor->cursor_manager->buffer = buffer;
        global_lle_editor->history_navigation_pos = 0;
        global_lle_editor->history_nav_seen_count = 0;
    }
    
    // Step 3: Build readline context using persistent keybinding_manager
    readline_context_t ctx = {
        .buffer = buffer,
        .term = term,
        .prompt = prompt,
        .continuation_state = &continuation_state,
        .editor = global_lle_editor,
        // Use persistent keybinding_manager from editor - NOT created here
        .keybinding_manager = global_lle_editor ? 
                              global_lle_editor->keybinding_manager : NULL,
        .current_suggestion = NULL,
        .suppress_autosuggestion = false,
    };
    
    // Step 4: Enter raw mode and process input
    lle_unix_interface_enter_raw_mode(term->unix_interface);
    // ... keystroke event loop using event_system ...
    lle_unix_interface_exit_raw_mode(term->unix_interface);
    
    // Step 5: Cleanup per-readline components only
    continuation_state_cleanup(&continuation_state);
    lle_event_system_destroy(event_system);
    lle_terminal_abstraction_destroy(term);
    // Note: buffer content returned to caller, buffer struct cleaned up
    
    // Persistent components (keybinding_manager, history, kill_ring) 
    // remain in global_lle_editor for next readline call
    
    return result;
}
```

**Key change from current implementation**: The `keybinding_manager` is no longer
created per-readline call. It is initialized once during `lle_editor_create()` at
shell startup and persists for the shell session. User-defined keybindings set via
`bind` command or programmatically survive across commands.

---

## 5. Component Specifications

### 5.1 lle_shell_integration_t

Central structure managing shell-level LLE state.

```c
/**
 * Shell-level LLE integration state
 * Single global instance, initialized in init()
 */
typedef struct lle_shell_integration {
    // Initialization tracking
    lle_init_state_t init_state;
    
    // Persistent components
    lle_editor_t *editor;                    // Global editor
    lle_prompt_composer_t *prompt_composer;  // Spec 25
    lle_shell_event_hub_t *event_hub;        // Event routing
    
    // Safety state
    bool recovery_mode;      // True if in degraded/minimal mode
    bool ctrl_g_pressed;     // Ctrl+G abort flag
    uint32_t error_count;    // Consecutive errors
    
    // Statistics
    uint64_t total_readline_calls;
    uint64_t successful_readline_calls;
    uint64_t recovery_count;     // Hard resets or degraded mode entries
    uint64_t degraded_mode_time; // Total time spent in degraded mode (ms)
    
} lle_shell_integration_t;

// Global instance
extern lle_shell_integration_t *g_lle_integration;
```

### 5.2 lle_init_state_t

Tracks initialization progress for clean rollback.

```c
/**
 * Initialization state tracking
 * Used for incremental init and clean rollback
 *
 * NOTE: This tracks SHELL-LEVEL initialization only.
 * Per-readline components (buffer, terminal_abstraction, keystroke event_system)
 * are created/destroyed within each lle_readline() call and are NOT tracked here.
 */
typedef struct lle_init_state {
    // Core systems (Steps 1-3)
    bool memory_pool_initialized;    // Step 1
    bool terminal_initialized;       // Step 2
    bool event_hub_initialized;      // Step 3 - shell event hub, NOT per-readline
    
    // Editor and subsystems (Step 4)
    bool editor_initialized;
    bool cursor_manager_initialized;
    bool kill_ring_initialized;
    bool keybinding_manager_initialized;  // Now persistent in editor
    bool change_tracker_initialized;
    bool completion_system_initialized;
    bool widget_system_initialized;
    
    // Persistent features (Steps 5-6)
    bool history_initialized;        // Step 5
    bool prompt_initialized;         // Step 6
    bool theme_initialized;          // Part of Step 6
    
    // Shell integration (Step 7)
    bool shell_hooks_installed;      // Step 7
    bool display_bridge_connected;
    
} lle_init_state_t;
```

### 5.3 lle_shell_event_hub_t

Routes shell events to LLE components.

```c
/**
 * Shell event hub
 * Central routing for shell->LLE events
 */
typedef struct lle_shell_event_hub {
    // Event handlers
    lle_directory_changed_handler_t on_directory_changed;
    lle_pre_command_handler_t on_pre_command;
    lle_post_command_handler_t on_post_command;
    lle_prompt_display_handler_t on_prompt_display;
    
    // Handler user data
    void *directory_changed_data;
    void *pre_command_data;
    void *post_command_data;
    void *prompt_display_data;
    
    // State
    char current_directory[PATH_MAX];
    char previous_directory[PATH_MAX];
    int last_exit_code;
    uint64_t last_command_duration_us;
    uint64_t command_start_time_us;  // Set by pre_command for duration calc
    
} lle_shell_event_hub_t;
```

---

## 6. Shell Event System

> **EXTENSIBILITY NOTE - FUTURE USER HOOKS**
>
> The shell event hub architecture specified here is explicitly designed to support 
> future user-extensible hooks similar to ZSH's `preexec`, `precmd`, and `chpwd`.
>
> **Future capability (separate spec):**
> ```bash
> # ZSH-style user hook registration
> preexec myfunc1 myfunc2      # Run before command execution
> precmd notify_done           # Run after command, before prompt  
> chpwd update_window_title    # Run on directory change
> ```
>
> **Architecture:**
> ```
> Shell Event Hub (this spec)
>   +-- LLE_SHELL_EVENT_PRE_COMMAND
>         +-- Internal: Update prompt context
>         +-- Future: Iterate user preexec functions
>   +-- LLE_SHELL_EVENT_POST_COMMAND
>         +-- Internal: Record exit code, duration
>         +-- Future: Iterate user precmd functions
>   +-- LLE_SHELL_EVENT_DIRECTORY_CHANGED
>         +-- Internal: Update cwd, invalidate git cache
>         +-- Future: Iterate user chpwd functions
> ```
>
> This spec establishes the internal event infrastructure. User hook registration,
> error handling for user functions, and the builtin interface will be specified
> separately once this foundation is implemented and proven stable.

### 6.1 Event Types

```c
/**
 * Shell-level events that LLE needs to know about
 */
typedef enum {
    LLE_SHELL_EVENT_DIRECTORY_CHANGED,  // cd, pushd, popd
    LLE_SHELL_EVENT_PRE_COMMAND,        // Before command execution
    LLE_SHELL_EVENT_POST_COMMAND,       // After command execution
    LLE_SHELL_EVENT_PROMPT_DISPLAY,     // About to display prompt
    LLE_SHELL_EVENT_SHELL_EXIT,         // Shell is exiting
} lle_shell_event_type_t;
```

### 6.2 Event Data Structures

```c
/**
 * Directory changed event data
 */
typedef struct {
    const char *old_directory;
    const char *new_directory;
} lle_directory_changed_data_t;

/**
 * Pre-command event data
 */
typedef struct {
    const char *command;        // Command about to execute
    bool is_background;         // Running in background?
} lle_pre_command_data_t;

/**
 * Post-command event data
 */
typedef struct {
    const char *command;        // Command that executed
    int exit_code;              // Exit status
    uint64_t duration_us;       // Execution time in microseconds
    bool was_background;        // Was background job?
} lle_post_command_data_t;
```

### 6.3 Event Hook Installation

```c
/**
 * Install shell event hooks
 * Called during lle_shell_integration_init()
 */
lle_result_t lle_install_shell_event_hooks(void) {
    // Register hooks with shell subsystems
    
    // 1. Directory change hook (for cd, pushd, popd)
    //    Modifies: src/builtins_cd.c
    //    Hook point: After successful chdir()
    
    // 2. Pre-command hook
    //    Modifies: src/executor.c or parse_and_execute()
    //    Hook point: Before command execution begins
    
    // 3. Post-command hook  
    //    Modifies: src/lush.c main loop
    //    Hook point: After parse_and_execute() returns
    //    Note: Already have display_integration_post_command_update()
    
    // 4. Prompt display hook
    //    Modifies: src/readline_integration.c
    //    Hook point: Before lush_generate_prompt()
    
    return LLE_SUCCESS;
}
```

### 6.4 Event Firing Functions

```c
/**
 * @brief Fire directory changed event
 *
 * Called from cd/pushd/popd builtins after successful chdir().
 * Updates hub state and notifies prompt composer to invalidate
 * directory-dependent segments (e.g., git status).
 *
 * @param old_dir Previous working directory
 * @param new_dir New working directory
 */
void lle_fire_directory_changed(const char *old_dir, const char *new_dir) {
    if (!g_lle_integration || !config.use_lle) {
        return;  /* LLE not active */
    }
    
    lle_shell_event_hub_t *hub = g_lle_integration->event_hub;
    if (!hub || !hub->on_directory_changed) {
        return;
    }
    
    /* Update hub state */
    strncpy(hub->previous_directory, old_dir, PATH_MAX - 1);
    strncpy(hub->current_directory, new_dir, PATH_MAX - 1);
    
    /* Dispatch to handler */
    lle_directory_changed_data_t data = {
        .old_directory = old_dir,
        .new_directory = new_dir
    };
    
    hub->on_directory_changed(&data, hub->directory_changed_data);
    
    /* Update prompt composer context (Spec 25) */
    if (g_lle_integration->prompt_composer) {
        lle_prompt_context_set_cwd(g_lle_integration->prompt_composer->context,
                                   new_dir);
        /* Invalidate git segment cache (directory changed) */
        lle_prompt_invalidate_segment(g_lle_integration->prompt_composer, "git");
    }
}

/**
 * @brief Fire pre-command event
 *
 * Called from main loop before parse_and_execute().
 *
 * Use cases:
 * - Start command timing (for duration calculation in post-command)
 * - Transient prompt: replace fancy prompt with minimal version before execution
 * - Future: user preexec hooks
 * - Logging/auditing: record command about to execute
 *
 * @param command Command string about to execute
 * @param is_background True if command will run in background
 */
void lle_fire_pre_command(const char *command, bool is_background) {
    if (!g_lle_integration || !config.use_lle) {
        return;
    }
    
    lle_shell_event_hub_t *hub = g_lle_integration->event_hub;
    if (!hub) {
        return;
    }
    
    /* Record start time for duration calculation */
    hub->command_start_time_us = get_monotonic_time_us();
    
    /* Dispatch to handler */
    if (hub->on_pre_command) {
        lle_pre_command_data_t data = {
            .command = command,
            .is_background = is_background
        };
        hub->on_pre_command(&data, hub->pre_command_data);
    }
    
    /* Transient prompt: simplify displayed prompt before command runs
     * (keeps scrollback clean - fancy prompts only on current line) */
    if (g_lle_integration->prompt_composer) {
        lle_prompt_composer_enable_transient(g_lle_integration->prompt_composer);
    }
}

/**
 * @brief Fire post-command event
 *
 * Called from main loop after parse_and_execute().
 * Updates prompt context with exit code and duration for
 * status indicator segments.
 *
 * @param command Command string that was executed
 * @param exit_code Exit status of the command
 * @param duration_us Execution time in microseconds
 */
void lle_fire_post_command(const char *command, int exit_code,
                           uint64_t duration_us) {
    if (!g_lle_integration || !config.use_lle) {
        return;
    }
    
    lle_shell_event_hub_t *hub = g_lle_integration->event_hub;
    if (!hub) {
        return;
    }
    
    /* Update hub state */
    hub->last_exit_code = exit_code;
    hub->last_command_duration_us = duration_us;
    
    /* Dispatch to handler */
    if (hub->on_post_command) {
        lle_post_command_data_t data = {
            .command = command,
            .exit_code = exit_code,
            .duration_us = duration_us,
            .was_background = false
        };
        hub->on_post_command(&data, hub->post_command_data);
    }
    
    /* Update prompt composer context (Spec 25) */
    if (g_lle_integration->prompt_composer) {
        lle_prompt_context_set_exit_code(
            g_lle_integration->prompt_composer->context, exit_code);
        lle_prompt_context_set_duration(
            g_lle_integration->prompt_composer->context, duration_us);
    }
}
```

---

## 7. Safety Mechanisms

### 7.1 Reset Hierarchy

LLE provides a three-tier reset hierarchy that does NOT depend on GNU readline:

| Level | Trigger | Effect | Preserves |
|-------|---------|--------|-----------|
| **Soft** | Normal Ctrl+G (3rd press) | Abort current line, return empty | Everything - next readline starts fresh |
| **Hard** | Panic Ctrl+G (3x in 500ms) or `lle reset` | Destroy/recreate global_lle_editor | History file, config, terminal scrollback |
| **Nuclear** | `lle reset --terminal` | Hard reset + terminal reset sequence | History file, config only |

**Key Design Principle**: These mechanisms are self-contained within LLE. They do NOT fall back to GNU readline. When readline is removed from lush, these mechanisms continue to work exactly the same way.

### 7.2 Soft Reset (Normal Ctrl+G)

The current tiered Ctrl+G behavior, which works well:

```c
/**
 * @brief Tiered Ctrl+G - Normal abort behavior
 * 
 * This is NOT a reset - it's dismissal/abort. The next lle_readline()
 * call naturally creates fresh per-readline components anyway.
 *
 * Behavior:
 * 1. First press: Dismiss completion menu if visible
 * 2. Second press: Clear autosuggestion if visible  
 * 3. Third press (or first if nothing to dismiss): Abort line, return ""
 *
 * @param ctx Readline context
 * @return LLE_SUCCESS
 */
lle_result_t lle_abort_line_context(readline_context_t *ctx) {
    /* Check completion menu first */
    if (completion_menu_visible(ctx)) {
        dismiss_completion_menu(ctx);
        return LLE_SUCCESS;  /* Don't abort yet */
    }
    
    /* Check autosuggestion second */
    if (has_autosuggestion(ctx)) {
        clear_autosuggestion(ctx);
        return LLE_SUCCESS;  /* Don't abort yet */
    }
    
    /* Nothing to dismiss - abort line */
    *ctx->done = true;
    *ctx->final_line = strdup("");
    return LLE_SUCCESS;
}
```

**What happens next**: The current `lle_readline()` returns empty string. Main loop shows fresh prompt. Next `lle_readline()` call creates fresh buffer, event system, terminal abstraction - all per-readline components are new.

### 7.3 Hard Reset (Panic Ctrl+G)

Destroys and recreates the global editor when LLE state is corrupted:

```c
/**
 * @brief Panic Ctrl+G detection
 *
 * Triple Ctrl+G within 500ms triggers hard reset.
 */
static volatile sig_atomic_t ctrl_g_count = 0;
static volatile uint64_t last_ctrl_g_time = 0;

void lle_check_panic_ctrl_g(void) {
    uint64_t now = get_monotonic_time_ms();
    
    if (now - last_ctrl_g_time > 500) {
        ctrl_g_count = 0;  /* Reset if too slow */
    }
    
    ctrl_g_count++;
    last_ctrl_g_time = now;
    
    if (ctrl_g_count >= 3) {
        lle_hard_reset();
        ctrl_g_count = 0;
    }
}

/**
 * @brief Hard reset - destroy and recreate global editor
 * 
 * This fixes state corruption in persistent components:
 * - Kill ring in weird state
 * - History navigation stuck
 * - Widget registry corrupted
 * - Keybinding manager broken
 *
 * Does NOT reset terminal (preserves scrollback).
 */
void lle_hard_reset(void) {
    /* 1. Save history before destroying (don't lose user's commands) */
    if (global_lle_editor && global_lle_editor->history_system) {
        lle_history_save_to_file(global_lle_editor->history_system,
                                 get_lle_history_path());
    }
    
    /* 2. Force exit raw mode (ensure terminal is usable) */
    lle_unix_interface_force_exit_raw_mode();
    
    /* 3. Destroy global editor completely */
    if (global_lle_editor) {
        lle_editor_destroy(global_lle_editor);
        global_lle_editor = NULL;
    }
    
    /* 4. Notify user */
    const char *msg = "\r\n[LLE Reset: Editor reinitialized]\r\n";
    write(STDERR_FILENO, msg, strlen(msg));
    
    /* 5. Next lle_readline() call will recreate global_lle_editor
     *    with fresh state (but will reload history from file) */
}
```

**What is lost**: Kill ring contents, history navigation position, any in-memory cached state.

**What is preserved**: History file (saved before reset), config settings, terminal scrollback.

### 7.4 Nuclear Reset (lle reset --terminal)

For when the terminal itself is corrupted:

```c
/**
 * @brief Nuclear reset - hard reset plus terminal reset
 * 
 * Use when:
 * - Terminal is displaying garbage
 * - Escape sequences corrupted display
 * - Raw mode state is inconsistent
 *
 * @warning Clears terminal screen and scrollback!
 */
void lle_nuclear_reset(void) {
    /* 1. Perform hard reset first */
    lle_hard_reset();
    
    /* 2. Send terminal reset sequence
     *    \033c    = Full reset (RIS - Reset to Initial State)
     *    \033[!p  = Soft terminal reset (DECSTR)
     *    \033[2J  = Clear screen
     *    \033[H   = Cursor to home */
    const char *reset_seq = "\033c\033[2J\033[H";
    write(STDOUT_FILENO, reset_seq, strlen(reset_seq));
    
    /* 3. Notify user */
    const char *msg = "[LLE Reset: Terminal and editor reinitialized]\n";
    write(STDERR_FILENO, msg, strlen(msg));
}
```

### 7.5 Display Builtin: LLE Reset Subcommands

LLE reset functionality is exposed through the existing `display` builtin, which
already handles `display lle enable` and `display lle disable`. This consolidates
all LLE control in one place rather than creating a separate `lle` builtin.

**New subcommands:**
- `display lle reset` - Hard reset (same as panic Ctrl+G)
- `display lle reset --terminal` - Nuclear reset (clears screen)
- `display lle status` - Show LLE state information

```c
/**
 * @brief Handle 'display lle' subcommands
 *
 * Extends existing display lle enable/disable with reset and status.
 *
 * @param argc Argument count (after 'display lle')
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
static int handle_display_lle(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: display lle <enable|disable|reset|status>\n");
        return 1;
    }
    
    if (strcmp(argv[0], "enable") == 0) {
        /* Existing enable logic */
        return display_lle_enable();
    }
    
    if (strcmp(argv[0], "disable") == 0) {
        /* Existing disable logic */
        return display_lle_disable();
    }
    
    if (strcmp(argv[0], "reset") == 0) {
        if (argc >= 2 && strcmp(argv[1], "--terminal") == 0) {
            lle_nuclear_reset();
        } else {
            lle_hard_reset();
        }
        return 0;
    }
    
    if (strcmp(argv[0], "status") == 0) {
        lle_print_status();
        return 0;
    }
    
    fprintf(stderr, "Unknown display lle command: %s\n", argv[0]);
    return 1;
}
```

### 7.6 Error Counter and Auto-Reset

Track consecutive errors and auto-trigger hard reset:

```c
/** @brief Maximum consecutive errors before auto-reset */
#define LLE_MAX_CONSECUTIVE_ERRORS 5

/**
 * @brief Record an LLE error and check for auto-reset threshold
 *
 * Tracks consecutive errors. When LLE_MAX_CONSECUTIVE_ERRORS is reached,
 * automatically triggers a hard reset to recover.
 *
 * @param error The error result to record
 */
void lle_record_error(lle_result_t error) {
    if (!g_lle_integration) return;
    
    g_lle_integration->error_count++;
    
    if (g_lle_integration->error_count >= LLE_MAX_CONSECUTIVE_ERRORS) {
        log_warning("LLE: %d consecutive errors, performing hard reset",
                    LLE_MAX_CONSECUTIVE_ERRORS);
        lle_hard_reset();
        g_lle_integration->error_count = 0;
    }
}

/**
 * @brief Reset the consecutive error counter
 *
 * Called after successful readline completion to clear the error count.
 */
void lle_reset_error_counter(void) {
    if (g_lle_integration) {
        g_lle_integration->error_count = 0;
    }
}
```

### 7.7 Watchdog Timer

Detect hung `lle_readline()` calls:

```c
/** @brief Readline timeout before watchdog triggers (30 seconds) */
#define LLE_READLINE_TIMEOUT_MS 30000

/**
 * @brief Watchdog signal handler for hung readline detection
 *
 * Called by SIGALRM when readline exceeds timeout. Triggers hard reset
 * to recover from stuck state.
 *
 * @param sig Signal number (unused, always SIGALRM)
 *
 * @warning Uses write() directly - signal-safe, no stdio
 */
static void lle_watchdog_handler(int sig) {
    (void)sig;
    
    const char *msg = "\r\n[LLE Watchdog: Readline timeout, resetting...]\r\n";
    write(STDERR_FILENO, msg, strlen(msg));
    
    lle_hard_reset();
}

/**
 * @brief Start the readline watchdog timer
 *
 * Call at the beginning of lle_readline(). Sets SIGALRM to fire
 * after LLE_READLINE_TIMEOUT_MS milliseconds.
 */
void lle_start_watchdog(void) {
    struct sigaction sa = {0};
    sa.sa_handler = lle_watchdog_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
    
    alarm(LLE_READLINE_TIMEOUT_MS / 1000);
}

/**
 * @brief Stop the readline watchdog timer
 *
 * Call at the end of lle_readline() to cancel the alarm.
 */
void lle_stop_watchdog(void) {
    alarm(0);
}
```

### 7.8 Signal-Level Ctrl+G Handler

**CRITICAL**: Ctrl+G must work even when the keybinding system is broken:

```c
/**
 * @brief Low-level Ctrl+G emergency handler
 *
 * Installed at signal/terminal level, NOT through keybinding manager.
 * This ensures recovery works even if:
 * - Event system is broken
 * - Keybinding manager is corrupted
 * - Buffer is in invalid state
 * - Main loop is stuck
 *
 * The raw terminal read loop checks for Ctrl+G (0x07) directly
 * before dispatching to the event system.
 *
 * @param byte Raw byte from terminal read
 * @return false always (allows normal keybinding to also process)
 *
 * @warning This function must remain minimal and never allocate memory
 */
static bool check_emergency_ctrl_g(uint8_t byte) {
    if (byte != 0x07) {
        return false;
    }
    
    /* Check for panic sequence (3x in 500ms) */
    lle_check_panic_ctrl_g();
    
    /* Let normal keybinding system also handle it for tiered dismiss */
    return false;
}
```

---

## 8. GNU Readline Coexistence

### 8.1 Mode Selection

The mode selection logic in `readline_integration.c` remains unchanged:

```c
char *lush_readline_with_prompt(const char *prompt) {
    // ... prompt generation ...
    
    if (config.use_lle) {
        line = lle_readline(actual_prompt);
    } else {
        line = readline(actual_prompt);  // GNU readline
    }
    
    // ... post-processing ...
}
```

### 8.2 Shared Components

Both modes share:
- `config` structure (user preferences)
- Symbol table (PS1/PS2 variables)
- Display integration (layered display system)

### 8.3 Independent Components

| Component | GNU Readline | LLE |
|-----------|--------------|-----|
| History storage | `~/.lush_history` | `~/.lush_history_lle` |
| History API | `add_history()` | `lle_history_add_entry()` |
| Prompt generation | `build_prompt()` | `lle_prompt_composer_render()` |
| Completion | readline completion | `lle_completion_system` |
| Key bindings | inputrc | `lle_keybinding_manager` |
| Theme system | `src/themes.c` | Spec 25 `lle_theme_t` |

### 8.4 Runtime Switching

Runtime switching between LLE and GNU readline is supported via `display lle enable`
and `display lle disable`. The change takes effect immediately on the next readline
call - no shell restart required.

```c
#ifdef HAVE_READLINE
/**
 * @brief Switch between LLE and GNU readline at runtime
 *
 * Only available when both readline modes are built.
 * Use `display lle enable` or `display lle disable` from shell.
 *
 * @param use_lle True to enable LLE, false for GNU readline
 * @return LLE_SUCCESS on success, error code on failure
 */
lle_result_t lle_set_mode(bool use_lle) {
    if (use_lle == config.use_lle) {
        return LLE_SUCCESS;
    }
    
    if (use_lle && !g_lle_integration) {
        /* Attempt to initialize LLE */
        lle_result_t result = lle_shell_integration_init();
        if (result != LLE_SUCCESS) {
            return result;
        }
    }
    
    config.use_lle = use_lle;
    
    /* Takes effect on next readline() call */
    return LLE_SUCCESS;
}
#endif /* HAVE_READLINE */
```

---

## 9. Build Configuration

### 9.1 Meson Build Options

```meson
# meson_options.txt

option('readline', type: 'feature', value: 'auto',
       description: 'GNU Readline support')

option('lle', type: 'feature', value: 'enabled',
       description: 'Lush Line Editor support')

option('lle_default', type: 'boolean', value: false,
       description: 'Use LLE as default instead of GNU readline')
```

### 9.2 Build Configurations

| Configuration | readline | lle | Default Editor |
|--------------|----------|-----|----------------|
| Full (default) | enabled | enabled | GNU readline |
| LLE-first | enabled | enabled | LLE |
| LLE-only | disabled | enabled | LLE (no fallback) |
| Readline-only | enabled | disabled | GNU readline |

### 9.3 Conditional Compilation

```c
// In config.h or generated header

#cmakedefine HAVE_READLINE 1
#cmakedefine HAVE_LLE 1
#cmakedefine LLE_IS_DEFAULT 1

// In readline_integration.c

char *lush_readline_with_prompt(const char *prompt) {
#if defined(HAVE_LLE) && defined(HAVE_READLINE)
    // Both available - use config to select
    if (config.use_lle) {
        return lle_readline(actual_prompt);
    } else {
        return readline(actual_prompt);
    }
#elif defined(HAVE_LLE)
    // LLE only
    return lle_readline(actual_prompt);
#elif defined(HAVE_READLINE)
    // GNU readline only
    return readline(actual_prompt);
#else
    #error "Either readline or LLE must be enabled"
#endif
}
```

---

## 10. Error Handling

### 10.1 Error Categories

```c
/**
 * @brief LLE error categories for logging and recovery decisions
 */
typedef enum {
    /* Initialization errors */
    LLE_INIT_ERROR_MEMORY,           /**< Memory allocation failed */
    LLE_INIT_ERROR_TERMINAL,         /**< Terminal detection failed */
    LLE_INIT_ERROR_EDITOR,           /**< Editor creation failed */
    LLE_INIT_ERROR_HISTORY,          /**< History system failed */
    LLE_INIT_ERROR_PROMPT,           /**< Prompt system failed */
    LLE_INIT_ERROR_HOOKS,            /**< Shell hook installation failed */
    
    /* Runtime errors */
    LLE_RUNTIME_ERROR_BUFFER,        /**< Buffer operation failed */
    LLE_RUNTIME_ERROR_EVENT,         /**< Event processing failed */
    LLE_RUNTIME_ERROR_DISPLAY,       /**< Display update failed */
    LLE_RUNTIME_ERROR_TIMEOUT,       /**< Watchdog timeout */
    
    /* Recovery events */
    LLE_RECOVERY_CTRL_G,             /**< User-initiated recovery */
    LLE_RECOVERY_AUTO,               /**< Automatic recovery */
    LLE_RECOVERY_WATCHDOG,           /**< Watchdog-initiated */
    
} lle_error_category_t;
```

### 10.2 Error Response Matrix

| Error | Severity | Response (with readline) | Response (LLE-only) |
|-------|----------|--------------------------|---------------------|
| Memory allocation | Critical | Fallback to readline | Hard reset + degraded mode |
| Terminal detection | Warning | Continue with defaults | Continue with defaults |
| Editor creation | Critical | Fallback to readline | Hard reset + degraded mode |
| History load | Warning | Start empty history | Start empty history |
| Prompt system | Warning | Use fallback prompts | Use fallback prompts |
| Shell hooks | Warning | No auto-update | No auto-update |
| Buffer operation | Error | Abort current readline | Abort current readline |
| Display update | Warning | Skip display update | Skip display update |
| Timeout | Critical | Force recovery | Hard reset |

### 10.3 Logging

```c
/**
 * @brief LLE logging levels
 */
typedef enum {
    LLE_LOG_DEBUG,    /**< Detailed debugging */
    LLE_LOG_INFO,     /**< Informational */
    LLE_LOG_WARNING,  /**< Recoverable issues */
    LLE_LOG_ERROR,    /**< Errors requiring attention */
    LLE_LOG_FATAL,    /**< Unrecoverable errors */
} lle_log_level_t;

/**
 * @brief Log to stderr with timestamp and level
 *
 * @param level Severity level
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void lle_log(lle_log_level_t level, const char *format, ...);

/** @brief Convenience macros for logging */
#define lle_debug(...)   lle_log(LLE_LOG_DEBUG, __VA_ARGS__)
#define lle_info(...)    lle_log(LLE_LOG_INFO, __VA_ARGS__)
#define lle_warning(...) lle_log(LLE_LOG_WARNING, __VA_ARGS__)
#define lle_error(...)   lle_log(LLE_LOG_ERROR, __VA_ARGS__)
```

---

## 11. Testing Requirements

### 11.1 Unit Tests

```c
// tests/lle/test_initialization.c

void test_init_full_success(void);           // All components initialize
void test_init_editor_failure_fallback(void); // Editor fails -> readline
void test_init_history_failure_continue(void); // History fails -> continue
void test_init_prompt_failure_continue(void);  // Prompt fails -> fallback
void test_init_rollback_on_failure(void);     // Clean rollback
void test_shutdown_saves_history(void);       // History saved on exit
```

### 11.2 Integration Tests

```c
// tests/integration/test_lle_shell_integration.c

void test_cd_fires_directory_changed(void);   // cd triggers event
void test_post_command_updates_prompt(void);  // Exit code in prompt
void test_ctrl_g_recovery(void);              // Triple Ctrl+G fallback
void test_watchdog_recovery(void);            // Timeout fallback
void test_mode_switching(void);               // Toggle LLE/readline
```

### 11.3 Stress Tests

```c
// tests/stress/test_lle_stability.c

void test_1000_readline_calls(void);          // No memory leaks
void test_rapid_mode_switching(void);         // Toggle stability
void test_error_recovery_loop(void);          // Repeated failures
void test_concurrent_signals(void);           // Signal handling
```

### 11.4 Regression Tests

```c
// tests/regression/test_readline_compatibility.c

void test_gnu_readline_unchanged(void);       // Readline still works
void test_history_file_format(void);          // Backward compatible
void test_config_file_parsing(void);          // Config options work
void test_theme_fallback(void);               // Theme system fallback
```

---

## 12. Implementation Phases

### Phase 1: Core Infrastructure

**Goal**: Create initialization framework without breaking anything.

1. Create `lle_shell_integration.h` with type definitions
2. Create `lle_shell_integration.c` with init/shutdown
3. Add `lle_shell_integration_init()` call to `init.c`
4. Add `lle_shell_integration_shutdown()` to atexit handlers
5. Verify: GNU readline still works, LLE still works, no regressions

### Phase 2: Safety Mechanisms

**Goal**: Implement Ctrl+G recovery and watchdog.

1. Implement triple-Ctrl+G hard reset
2. Implement error counter and auto-reset
3. Implement watchdog timer
4. Add unit tests for recovery mechanisms
5. Verify: Recovery works in all failure scenarios

### Phase 3: Shell Event Hooks

**Goal**: Wire shell events to LLE.

1. Add directory change hook to cd/pushd/popd
2. Add pre-command hook before execution
3. Add post-command hook to main loop
4. Create `lle_shell_event_hub` implementation
5. Wire events to prompt composer (Spec 25)
6. Verify: Prompt updates on cd, shows exit code

### Phase 4: Spec 25 Integration

**Goal**: Enable and test prompt/theme system.

1. Initialize `lle_prompt_composer` in shell init
2. Connect prompt composer to shell events
3. Add theme loading and switching
4. Test prompt segments (git, time, status)
5. Verify: Full Spec 25 functionality working

### Phase 5: Polish and Documentation

**Goal**: Complete testing and documentation.

1. Complete all unit tests
2. Complete integration tests
3. Complete stress tests
4. Update user documentation
5. Final code review and cleanup

---

## 13. API Reference

### 13.1 Shell Integration API

```c
/**
 * @brief Initialize LLE shell integration
 *
 * Called once during shell startup. On failure:
 * - With GNU readline: falls back to readline
 * - LLE-only build: attempts hard reset, then degraded mode
 *
 * @return LLE_SUCCESS on success or successful fallback
 */
lle_result_t lle_shell_integration_init(void);

/**
 * @brief Shutdown LLE shell integration
 *
 * Called during shell exit (via atexit). Saves history and
 * cleans up all initialized components.
 */
void lle_shell_integration_shutdown(void);

/**
 * @brief Get shell integration instance
 *
 * @return Global integration instance or NULL if not initialized
 */
lle_shell_integration_t *lle_get_shell_integration(void);

/**
 * @brief Check if LLE is active
 *
 * @return true if LLE is initialized and active
 */
bool lle_is_active(void);
```

### 13.2 Shell Event API

```c
/**
 * @brief Fire directory changed event
 *
 * @param old_dir Previous working directory
 * @param new_dir New working directory
 */
void lle_fire_directory_changed(const char *old_dir, const char *new_dir);

/**
 * @brief Fire pre-command event
 *
 * @param command Command string about to execute
 * @param is_background True if command runs in background
 */
void lle_fire_pre_command(const char *command, bool is_background);

/**
 * @brief Fire post-command event
 *
 * @param command Command that was executed
 * @param exit_code Exit status of the command
 * @param duration_us Execution duration in microseconds
 */
void lle_fire_post_command(const char *command, int exit_code,
                           uint64_t duration_us);

/**
 * @brief Register event handler
 *
 * @param event_type Type of event to handle
 * @param handler Callback function
 * @param user_data Opaque pointer passed to handler
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_register_shell_event_handler(
    lle_shell_event_type_t event_type,
    void (*handler)(void *data, void *user_data),
    void *user_data);
```

### 13.3 Recovery API

```c
/**
 * @brief Perform hard reset of LLE
 *
 * Destroys and recreates the global editor. Preserves history file,
 * config, and terminal scrollback.
 */
void lle_hard_reset(void);

/**
 * @brief Perform nuclear reset of LLE
 *
 * Hard reset plus terminal reset (clears screen, restores sane state).
 * Preserves only history file and config.
 */
void lle_nuclear_reset(void);

/**
 * @brief Check if in recovery/degraded mode
 *
 * @return true if LLE is running with reduced functionality
 */
bool lle_is_recovery_mode(void);

/**
 * @brief Attempt to restore full LLE after degraded mode
 *
 * @return LLE_SUCCESS if full functionality restored
 */
lle_result_t lle_restore_from_recovery(void);
```

---

## Appendix A: Migration Checklist

### Files to Modify

1. **src/init.c**
   - Add `#include "lle/lle_shell_integration.h"`
   - Add `lle_shell_integration_init()` call after display integration
   - Add `atexit(lle_shell_integration_shutdown)`

2. **src/builtins_cd.c** (and pushd/popd)
   - Add `lle_fire_directory_changed()` call after successful chdir

3. **src/lush.c**
   - Add `lle_fire_pre_command()` call before command execution
   - Add `lle_fire_post_command()` call in main loop

4. **src/builtins_display.c**
   - Add `display lle reset` subcommand (hard reset)
   - Add `display lle reset --terminal` subcommand (nuclear reset)
   - Update `display lle status` if needed for new diagnostics

5. **meson.build**
   - Add new source files
   - Add build options

### New Files to Create

1. `include/lle/lle_shell_integration.h`
2. `src/lle/lle_shell_integration.c`
3. `src/lle/lle_shell_event_hub.c`
4. `src/lle/lle_safety.c`
5. `tests/lle/test_initialization.c`
6. `tests/integration/test_lle_shell_integration.c`

---

## Appendix B: Glossary

| Term | Definition |
|------|------------|
| Shell-level | Components that persist for the entire shell session |
| Readline-level | Components created/destroyed per `lle_readline()` call |
| Degraded mode | State where LLE runs with reduced functionality after recovery failure |
| Hard reset | Destroy and recreate global editor (preserves history, config, scrollback) |
| Nuclear reset | Hard reset plus terminal reset (preserves only history file and config) |
| Shell event | Notification of shell state change (cd, command, etc.) |

---

**Document Classification**: Core Architecture Specification  
**Revision History**: Created 2025-12-26  
**Next Review**: After Phase 1 implementation  
**Maintainer**: Lush Development Team
