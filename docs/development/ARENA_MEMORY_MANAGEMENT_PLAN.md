# Arena-Based Memory Management Implementation Plan

**Project**: Lush Line Editor (LLE)  
**Document**: Memory Management Architecture Redesign  
**Version**: 1.1.0  
**Date**: 2026-01-02  
**Status**: OK IMPLEMENTED (Phases 1-5 Complete)

---

## Implementation Status

| Phase | Description | Status | Date |
|-------|-------------|--------|------|
| Phase 1 | Arena infrastructure (`arena.h`, `arena.c`) | OK Complete | 2026-01-02 |
| Phase 2 | Session arena in `lle_shell_integration.c` | OK Complete | 2026-01-02 |
| Phase 3 | Edit-session arena in `lle_readline.c` | OK Complete | 2026-01-02 |
| Phase 4 | Event arena - critical input leak fix | OK Complete | 2026-01-02 |
| Phase 5 | Frame arena in `render_controller.c` | OK Complete | 2026-01-02 |
| Phase 6 | Cleanup unused GC code | Deferred | - |

### Files Created
- `include/lle/arena.h` - Complete arena API (~180 lines)
- `src/lle/core/arena.c` - Full implementation (~400 lines)

### Files Modified
- `src/lle/meson.build` - Added arena.c to build
- `include/lle/lle_shell_integration.h` - Added session_arena field
- `src/lle/lle_shell_integration.c` - Session arena lifecycle
- `src/lle/lle_readline.c` - Edit arena lifecycle
- `include/lle/terminal_abstraction.h` - Added event_arena to input processor
- `src/lle/terminal/terminal_input_processor.c` - Fixed critical per-keystroke leak
- `include/lle/display_integration.h` - Added frame_arena to render controller
- `src/lle/display/render_controller.c` - Frame arena lifecycle

### Critical Bug Fixed
The per-keystroke memory leak in `terminal_input_processor.c:186` has been fixed.
Input events are now allocated from the event arena and automatically freed
when the arena is reset at each iteration.

---

## Executive Summary

This document describes the planned replacement of LLE's current manual memory management approach with a hierarchical arena-based allocator. The new system provides automatic lifetime-scoped memory cleanup, eliminating memory leaks by design while improving performance and code simplicity.

### Key Benefits

| Benefit | Description |
|---------|-------------|
| **Zero memory leaks** | Memory freed automatically when arena destroyed |
| **O(1) allocation** | Bump-pointer allocation, no free-list management |
| **O(1) destruction** | Free entire arena in one operation |
| **Clearer ownership** | Lifetime tied to program scope, not manual tracking |
| **Simpler code** | No error-path cleanup, no `free()` calls scattered throughout |

---

## Table of Contents

1. [Problem Analysis](#1-problem-analysis)
2. [Solution: Hierarchical Arenas](#2-solution-hierarchical-arenas)
3. [Arena Data Structures](#3-arena-data-structures)
4. [Arena API Specification](#4-arena-api-specification)
5. [Integration with Existing Pool System](#5-integration-with-existing-pool-system)
6. [Arena Hierarchy for LLE](#6-arena-hierarchy-for-lle)
7. [Implementation Phases](#7-implementation-phases)
8. [File-by-File Migration Guide](#8-file-by-file-migration-guide)
9. [Success Criteria](#9-success-criteria)
10. [Risk Assessment and Mitigation](#10-risk-assessment-and-mitigation)

---

## 1. Problem Analysis

### 1.1 Current Architecture

The current LLE memory system consists of two layers:

**Lush Memory Pool** (`src/lush_memory_pool.c`):
- Pre-allocated slab allocator with 4 size classes (128B, 512B, 4KB, 16KB)
- Thread-safe with mutex protection
- Malloc fallback for sizes > 16KB
- Well-designed and performant

**LLE Memory Wrapper** (`src/lle/core/memory_management.c`):
- Thin wrapper: `lle_pool_alloc()` / `lle_pool_free()` call lush pool
- Allocation tracking table (4096 entries)
- Complex GC infrastructure that is partially implemented but never actually invoked

### 1.2 The Problem

**Spec 15** describes "Zero Memory Leaks: Comprehensive tracking and automatic cleanup" but the implementation requires callers to explicitly call `lle_pool_free()`. This doesn't happen consistently:

1. **Missing frees**: Many code paths allocate but never free (especially error paths)
2. **`calloc()` bypass**: ~100 `calloc()` calls bypass the pool system entirely
3. **Lifecycle confusion**: No clear ownership model - who is responsible for freeing?
4. **GC not running**: The sophisticated GC infrastructure exists but isn't integrated

### 1.3 Critical Memory Leaks Identified

| File | Line | Issue | Impact |
|------|------|-------|--------|
| `terminal_input_processor.c` | 186 | `calloc()` for input events, never freed | **Per-keystroke leak** |
| `lle_shell_integration.c` | 163 | `calloc()` for integration struct | Once per session |
| `event_timer.c` | 236-242 | `calloc()` for timer events | Per timer |
| `event_system.c` | 363 | `lle_pool_alloc()` for events, manual free missed | Per event |
| `render_controller.c` | 413-423 | `lle_pool_alloc()` for render output, error paths leak | Per render |
| `completion_system.c` | 108 | Completion results leaked on error | Per TAB |

### 1.4 Why Not Just Add Frees?

Adding individual `free()` calls is:
- **Error-prone**: Easy to miss, especially in error paths
- **Tedious**: Requires tracking every allocation
- **Fragile**: Refactoring can break cleanup
- **Spec-violating**: Spec 15 promises automatic cleanup

The architecturally correct solution is to make cleanup automatic.

---

## 2. Solution: Hierarchical Arenas

### 2.1 What is an Arena Allocator?

An arena (also called region, zone, or bump allocator) is a memory allocation strategy where:

1. Memory is allocated by incrementing a pointer (O(1) - very fast)
2. Individual frees are not supported
3. All memory is freed at once when the arena is destroyed

### 2.2 Why Arenas for LLE?

LLE's allocation patterns have clear **lifetime scopes**:

| Scope | Duration | Example Allocations |
|-------|----------|---------------------|
| Shell Session | Process lifetime | Global editor, history, keybindings |
| Edit Session | One `lle_readline()` call | Completion state, render cache |
| Event Processing | One keystroke | Input event, dispatch state |
| Render Frame | One display refresh | Render output buffers |

Arenas map perfectly to these scopes. Allocate from the appropriate arena, and everything is freed when that scope ends.

### 2.3 Hierarchical Arenas

Child arenas are automatically freed when their parent is freed:

```
Session Arena (freed on shell exit)
    |
    +-- Edit Arena (freed on readline return)
            |
            +-- Event Arena (freed after each keystroke)
            |
            +-- Frame Arena (freed after each render)
```

---

## 3. Arena Data Structures

### 3.1 Arena Chunk

Arenas allocate large chunks from the lush pool, then sub-allocate from those chunks:

```c
/**
 * Arena chunk - contiguous memory region allocated from lush pool
 * Forms a linked list for arenas that grow beyond initial size
 */
typedef struct lle_arena_chunk_t {
    struct lle_arena_chunk_t *next;  /* Next chunk in chain (NULL if last) */
    size_t size;                      /* Usable size of this chunk */
    size_t used;                      /* Bytes allocated from this chunk */
    char data[];                      /* Flexible array - actual memory */
} lle_arena_chunk_t;
```

### 3.2 Arena Structure

```c
/**
 * Arena allocator - bump-pointer allocator with hierarchical lifetime
 */
typedef struct lle_arena_t {
    /* Identity and hierarchy */
    const char *name;                 /* Debug name (e.g., "edit_session") */
    struct lle_arena_t *parent;       /* Parent arena (NULL for root) */
    struct lle_arena_t *first_child;  /* First child arena */
    struct lle_arena_t *next_sibling; /* Next sibling in parent's child list */
    
    /* Memory management */
    lle_arena_chunk_t *first_chunk;   /* First chunk (always present) */
    lle_arena_chunk_t *current_chunk; /* Current allocation chunk */
    size_t default_chunk_size;        /* Size for new chunks (default 4KB) */
    size_t alignment;                 /* Default alignment (16 bytes) */
    
    /* Statistics (optional, compile-time flag) */
#ifdef LLE_ARENA_STATS
    size_t total_allocated;           /* Total bytes allocated */
    size_t allocation_count;          /* Number of allocations */
    size_t chunk_count;               /* Number of chunks */
#endif
} lle_arena_t;
```

---

## 4. Arena API Specification

### 4.1 Lifecycle Functions

```c
/**
 * Create a new arena
 *
 * @param parent    Parent arena (NULL for root arena)
 * @param name      Debug name for the arena
 * @param initial_size  Initial chunk size (0 = default 4KB)
 * @return New arena, or NULL on failure
 *
 * The arena is linked into parent's child list automatically.
 * When parent is destroyed, this arena will also be destroyed.
 */
lle_arena_t *lle_arena_create(lle_arena_t *parent, 
                               const char *name,
                               size_t initial_size);

/**
 * Destroy arena and all children
 *
 * @param arena Arena to destroy
 *
 * This function:
 * 1. Recursively destroys all child arenas (depth-first)
 * 2. Unlinks from parent's child list
 * 3. Frees all chunks back to lush_pool
 * 4. Frees arena structure itself
 *
 * After this call, all pointers allocated from this arena are INVALID.
 */
void lle_arena_destroy(lle_arena_t *arena);

/**
 * Reset arena to empty state (keep chunks allocated)
 *
 * @param arena Arena to reset
 *
 * Faster than destroy+create when reusing an arena for similar workload.
 * Does NOT destroy child arenas.
 */
void lle_arena_reset(lle_arena_t *arena);
```

### 4.2 Allocation Functions

```c
/**
 * Allocate memory from arena
 *
 * @param arena Arena to allocate from
 * @param size  Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 *
 * Memory is NOT zeroed. Use lle_arena_calloc for zeroed memory.
 * Memory is aligned to arena's default alignment (16 bytes).
 */
void *lle_arena_alloc(lle_arena_t *arena, size_t size);

/**
 * Allocate zeroed memory from arena
 */
void *lle_arena_calloc(lle_arena_t *arena, size_t count, size_t size);

/**
 * Allocate aligned memory from arena
 */
void *lle_arena_alloc_aligned(lle_arena_t *arena, size_t size, size_t alignment);

/**
 * Duplicate string in arena
 */
char *lle_arena_strdup(lle_arena_t *arena, const char *str);

/**
 * Duplicate string with length limit
 */
char *lle_arena_strndup(lle_arena_t *arena, const char *str, size_t max_len);
```

### 4.3 Utility Functions

```c
/**
 * Get total bytes allocated from arena
 */
size_t lle_arena_get_allocated(lle_arena_t *arena, bool include_children);

/**
 * Get number of chunks in arena
 */
size_t lle_arena_get_chunk_count(lle_arena_t *arena);

/**
 * Print arena statistics (debug)
 */
void lle_arena_print_stats(lle_arena_t *arena, int indent);
```

---

## 5. Integration with Existing Pool System

### 5.1 Chunk Allocation

Arenas allocate chunks from the existing lush pool:

```c
static lle_arena_chunk_t *arena_alloc_chunk(size_t min_size) {
    /* Include chunk header in allocation */
    size_t total_size = sizeof(lle_arena_chunk_t) + min_size;
    
    /* Round up to reasonable chunk size (4KB minimum) */
    if (total_size < 4096) {
        total_size = 4096;
    }
    
    /* Allocate from lush pool - uses existing infrastructure */
    lle_arena_chunk_t *chunk = lush_pool_alloc(total_size);
    if (!chunk) {
        return NULL;
    }
    
    chunk->next = NULL;
    chunk->size = total_size - sizeof(lle_arena_chunk_t);
    chunk->used = 0;
    
    return chunk;
}

static void arena_free_chunk(lle_arena_chunk_t *chunk) {
    lush_pool_free(chunk);
}
```

### 5.2 Bump-Pointer Allocation

```c
void *lle_arena_alloc(lle_arena_t *arena, size_t size) {
    if (!arena || size == 0) return NULL;
    
    /* Align size */
    size = (size + arena->alignment - 1) & ~(arena->alignment - 1);
    
    /* Check if current chunk has space */
    lle_arena_chunk_t *chunk = arena->current_chunk;
    if (chunk->used + size > chunk->size) {
        /* Need new chunk */
        size_t chunk_size = size > arena->default_chunk_size 
                          ? size 
                          : arena->default_chunk_size;
        lle_arena_chunk_t *new_chunk = arena_alloc_chunk(chunk_size);
        if (!new_chunk) return NULL;
        
        new_chunk->next = arena->first_chunk;
        arena->first_chunk = new_chunk;
        arena->current_chunk = new_chunk;
        chunk = new_chunk;
    }
    
    /* Bump pointer allocation - O(1) */
    void *ptr = chunk->data + chunk->used;
    chunk->used += size;
    
#ifdef LLE_ARENA_STATS
    arena->total_allocated += size;
    arena->allocation_count++;
#endif
    
    return ptr;
}
```

### 5.3 Coexistence During Migration

Both allocation systems work simultaneously:

```c
/* Old code continues to work */
void *old_style = lle_pool_alloc(size);
/* ... use old_style ... */
lle_pool_free(old_style);  /* Must remember to free */

/* New arena-based code */
void *arena_style = lle_arena_alloc(arena, size);
/* ... use arena_style ... */
/* Freed automatically when arena destroyed - no explicit free needed */
```

---

## 6. Arena Hierarchy for LLE

### 6.1 Session Arena

**Lifetime**: Shell process start to exit  
**Created in**: `lle_shell_integration_init()`  
**Destroyed in**: `lle_shell_integration_shutdown()`

**Allocations**:
- `lle_shell_integration_t` structure
- Global editor (`lle_editor_t`)
- Event hub
- Prompt composer
- History system
- Kill ring
- Keybinding manager
- Widget registry

### 6.2 Edit Arena

**Lifetime**: One `lle_readline()` call  
**Created in**: `lle_readline()` entry  
**Destroyed in**: `lle_readline()` exit

**Allocations**:
- `readline_context_t`
- Continuation state
- Current suggestion buffer
- Completion state and menu
- Completion results
- Context analyzer
- Render cache entries

### 6.3 Event Arena

**Lifetime**: One input event processing cycle  
**Created in**: Input loop iteration start  
**Destroyed in**: Input loop iteration end

**Allocations**:
- `lle_input_event_t` (fixes critical leak!)
- Event data copies
- Dispatch temporary state
- Handler temporary allocations

### 6.4 Frame Arena

**Lifetime**: One display refresh  
**Created in**: `lle_render_frame()` start  
**Destroyed in**: `lle_render_frame()` end

**Allocations**:
- `lle_render_output_t`
- Render content buffer
- Pipeline intermediate outputs
- Display composition buffers

---

## 7. Implementation Phases

### Phase 1: Arena Infrastructure

**Duration**: Week 1  
**Dependencies**: None

**New Files**:
- `include/lle/arena.h` - API header
- `src/lle/core/arena.c` - Implementation (~400 lines)

**Tasks**:
1. Implement `lle_arena_create()` with parent linking
2. Implement `lle_arena_destroy()` with recursive child destruction
3. Implement `lle_arena_alloc()` with bump pointer
4. Implement `lle_arena_reset()`
5. Implement utility functions (strdup, calloc, aligned)
6. Add unit tests
7. Add to build system

**Testing**:
- Unit tests for all arena operations
- Stress test with many allocations
- Verify parent-child relationships

### Phase 2: Session Arena

**Duration**: Week 2  
**Dependencies**: Phase 1

**Modified Files**:
- `src/lle/lle_shell_integration.c`
- `include/lle/lle_shell_integration.h`

**Tasks**:
1. Add `session_arena` field to `lle_shell_integration_t`
2. Create session arena in `lle_shell_integration_init()`
3. Migrate shell integration allocations to arena
4. Destroy session arena in `lle_shell_integration_shutdown()`

**Testing**:
- Shell starts and shuts down cleanly
- Valgrind shows no new leaks
- Existing functionality unchanged

### Phase 3: Edit-Session Arena

**Duration**: Week 3  
**Dependencies**: Phase 2

**Modified Files**:
- `src/lle/lle_readline.c`
- `src/lle/completion/completion_system.c`
- `src/lle/completion/context_analyzer.c`

**Tasks**:
1. Create edit arena at `lle_readline()` start
2. Migrate `readline_context_t` allocations
3. Migrate completion result/state allocations
4. Migrate context analyzer
5. Destroy edit arena before return

**Testing**:
- Single-line input works
- Multi-line input works
- Completion system works
- Valgrind verification

### Phase 4: Event Arena (Critical Leak Fix)

**Duration**: Week 4  
**Dependencies**: Phase 3

**Modified Files**:
- `src/lle/lle_readline.c` (input loop)
- `src/lle/terminal/terminal_input_processor.c`
- `src/lle/event/event_system.c`

**Tasks**:
1. Create event arena per input loop iteration
2. Change `terminal_input_processor.c:186` from `calloc` to arena
3. Migrate event dispatch temporary state
4. Remove now-unnecessary `lle_event_destroy()` calls
5. Destroy event arena at loop iteration end

**Testing**:
- Input events processed correctly
- No memory growth during extended editing
- Stress test with rapid input (1000+ keystrokes)

### Phase 5: Frame Arena

**Duration**: Week 5  
**Dependencies**: Phase 4

**Modified Files**:
- `src/lle/display/render_controller.c`
- `src/lle/display/render_pipeline.c`
- `src/lle/display/display_bridge.c`

**Tasks**:
1. Create frame arena per render
2. Migrate `lle_render_output_t` to arena
3. Migrate pipeline intermediate buffers
4. Destroy frame arena after display update

**Testing**:
- Display renders correctly
- No memory growth with rapid updates
- Window resize handling works

### Phase 6: Cleanup and Optimization

**Duration**: Week 6  
**Dependencies**: Phases 1-5

**Tasks**:
1. Remove unused GC code from `memory_management.c` (~1000 lines)
2. Simplify remaining pool functions
3. Update Spec 15 documentation to reflect arena approach
4. Add arena usage guidelines to developer docs
5. Performance benchmarking
6. Final comprehensive valgrind verification

---

## 8. File-by-File Migration Guide

### 8.1 terminal_input_processor.c (Priority 1 - Critical Leak)

**Current code (line 186)**:
```c
lle_input_event_t *new_event = calloc(1, sizeof(lle_input_event_t));
if (!new_event) {
    return LLE_ERROR_OUT_OF_MEMORY;
}
```

**New code**:
```c
/* Arena passed from caller or accessed via processor context */
lle_input_event_t *new_event = lle_arena_calloc(
    processor->event_arena, 1, sizeof(lle_input_event_t));
if (!new_event) {
    return LLE_ERROR_OUT_OF_MEMORY;
}
/* No free() needed - arena destruction handles cleanup */
```

### 8.2 lle_readline.c

**Current code**:
```c
char *lle_readline(const char *prompt) {
    readline_context_t ctx = {0};
    ctx.continuation_state = calloc(1, sizeof(continuation_state_t));
    ctx.current_suggestion = malloc(256);
    // ...
    
    while (!done) {
        // Read and process events
        // Memory leaked on each iteration!
    }
    
    free(ctx.continuation_state);
    free(ctx.current_suggestion);
    // Easy to miss frees in error paths
}
```

**New code**:
```c
char *lle_readline(const char *prompt) {
    /* Create edit arena - all per-readline allocations go here */
    lle_arena_t *edit_arena = lle_arena_create(
        g_lle_integration->session_arena, "edit", 8*1024);
    if (!edit_arena) return NULL;
    
    readline_context_t *ctx = lle_arena_calloc(edit_arena, 1, sizeof(*ctx));
    ctx->continuation_state = lle_arena_alloc(edit_arena, sizeof(continuation_state_t));
    ctx->current_suggestion = lle_arena_alloc(edit_arena, 256);
    
    while (!done) {
        /* Create per-event arena */
        lle_arena_t *event_arena = lle_arena_create(edit_arena, "event", 1024);
        
        lle_input_event_t *event = lle_arena_calloc(event_arena, 1, sizeof(*event));
        lle_unix_interface_read_event(unix_iface, event, timeout_ms);
        handle_event(event, ctx);
        
        /* Automatic cleanup - no individual frees needed */
        lle_arena_destroy(event_arena);
    }
    
    /* Copy result out before destroying arena */
    char *result = strdup(ctx->buffer->data);
    
    /* Single call frees ALL per-readline allocations */
    lle_arena_destroy(edit_arena);
    
    return result;
}
```

### 8.3 event_system.c

**Current code**:
```c
lle_event_t *lle_event_create(lle_event_type_t type, void *data, size_t data_size) {
    lle_event_t *evt = lle_pool_alloc(sizeof(lle_event_t));
    if (!evt) return NULL;
    
    if (data && data_size > 0) {
        evt->data = lle_pool_alloc(data_size);
        if (!evt->data) {
            lle_pool_free(evt);  // Easy to forget
            return NULL;
        }
        memcpy(evt->data, data, data_size);
    }
    return evt;
}

void lle_event_destroy(lle_event_t *evt) {
    if (evt) {
        lle_pool_free(evt->data);
        lle_pool_free(evt);
    }
}
```

**New code**:
```c
lle_event_t *lle_event_create(lle_arena_t *arena, lle_event_type_t type, 
                               void *data, size_t data_size) {
    lle_event_t *evt = lle_arena_alloc(arena, sizeof(lle_event_t));
    if (!evt) return NULL;
    
    if (data && data_size > 0) {
        evt->data = lle_arena_alloc(arena, data_size);
        if (!evt->data) {
            return NULL;  // No cleanup needed - arena handles it
        }
        memcpy(evt->data, data, data_size);
    }
    return evt;
}

/* lle_event_destroy() no longer needed - arena destruction handles cleanup */
```

---

## 9. Success Criteria

### 9.1 Functional Requirements

| Requirement | Verification Method |
|-------------|---------------------|
| Zero memory leaks | Valgrind reports 0 leaks in normal operation |
| All existing tests pass | Run full test suite |
| Input handling works | Manual testing + automated tests |
| Completion works | Manual testing + automated tests |
| Display rendering works | Manual testing + visual inspection |
| Multi-line editing works | Manual testing |

### 9.2 Performance Requirements

| Metric | Target | Verification |
|--------|--------|--------------|
| Allocation time | <= 100us (likely <1us with bump pointer) | Benchmark |
| Memory overhead | < 10% fragmentation waste | Memory analysis |
| No memory growth | Stable over 10,000 keystrokes | Long-running test |

### 9.3 Code Quality

| Metric | Target |
|--------|--------|
| Lines removed | ~1000 lines of unused GC code |
| Explicit free calls | Reduced by >80% |
| Error path cleanup | Automatic (no manual cleanup needed) |

---

## 10. Risk Assessment and Mitigation

### 10.1 Low Risk

| Risk | Mitigation |
|------|------------|
| Arena infrastructure bugs | Comprehensive unit tests before integration |
| Build system changes | Minimal - just add arena.c to build |

### 10.2 Medium Risk

| Risk | Mitigation |
|------|------------|
| Arena destroyed too early | Clear documentation of lifetimes; assertions in debug builds |
| Forgetting to pass arena | Compiler will error if arena parameter missing |
| Performance regression | Benchmark before/after; arenas should be faster |

### 10.3 Migration Risk Mitigation

- **Incremental approach**: One phase at a time, verify at each step
- **Coexistence**: Old `lle_pool_alloc()` continues to work during transition
- **Valgrind at each phase**: Catch regressions immediately
- **Rollback path**: Each phase can be reverted independently

---

## Appendix A: Comparison to Alternatives

### A.1 Why Not Manual Frees?

| Aspect | Manual Frees | Arenas |
|--------|--------------|--------|
| Leak prevention | Error-prone | Automatic |
| Error path cleanup | Must remember | Automatic |
| Code complexity | Scattered free() calls | One destroy() call |
| Refactoring safety | Fragile | Robust |

### A.2 Why Not Reference Counting?

| Aspect | Reference Counting | Arenas |
|--------|-------------------|--------|
| Overhead | Per-object counter | None |
| Cycles | Must handle manually | N/A |
| Implementation | Complex | Simple |
| Performance | Atomic ops | Pointer bump |

### A.3 Why Not Tracing GC?

| Aspect | Tracing GC | Arenas |
|--------|-----------|--------|
| Feasibility in C | Requires conservative scanning | Native |
| Pause times | Unpredictable | Deterministic |
| Implementation | Complex (Boehm GC) | Simple (~400 lines) |
| External dependency | Yes | No |

---

## Appendix B: Reference Implementations

The arena pattern is well-established in systems programming:

- **jemalloc arenas**: High-performance allocator used by Firefox, FreeBSD
- **Rust's bumpalo**: Arena allocator crate, widely used
- **Game engine frame allocators**: Unity, Unreal use per-frame arenas
- **Compilers**: LLVM, GCC use arenas for AST allocation
- **Databases**: SQLite uses arenas for query processing

---

## Appendix C: Files Summary

### New Files

| File | Description | Est. Lines |
|------|-------------|------------|
| `include/lle/arena.h` | Arena API header | ~150 |
| `src/lle/core/arena.c` | Arena implementation | ~400 |

### Modified Files

| File | Changes |
|------|---------|
| `src/lle/lle_shell_integration.c` | Add session arena lifecycle |
| `src/lle/lle_readline.c` | Add edit/event/frame arena management |
| `src/lle/terminal/terminal_input_processor.c` | Fix critical calloc leak |
| `src/lle/event/event_system.c` | Use event arena |
| `src/lle/display/render_controller.c` | Use frame arena |
| `src/lle/completion/completion_system.c` | Use edit arena |
| `src/lle/core/memory_management.c` | Remove unused GC code |
| `CMakeLists.txt` or `Makefile` | Add arena.c to build |

---

*This document supersedes the GC-based approach described in Spec 15. The arena-based approach achieves the same goals (automatic memory management, zero leaks) with a mechanism appropriate for C.*
