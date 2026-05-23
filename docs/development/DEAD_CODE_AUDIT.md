# Dead Code Audit - MAYBE_UNUSED Functions and Variables

**Date**: 2025-12-24
**Session**: 58
**Auditor**: AI Assistant

**Reading note (2026-05-23):** point-in-time snapshot. The file:line
references and per-symbol classifications were accurate as of the
audit date but have not been re-verified against current code. Use
this document as a *historical* record of the categorization
methodology and the major action calls; re-grep for `MAYBE_UNUSED`
and `LLE_MAYBE_UNUSED` to get an accurate current inventory before
acting on individual entries.

## Overview

This document catalogs all functions and variables marked with `MAYBE_UNUSED` or 
`LLE_MAYBE_UNUSED` in the lush codebase. Each item is categorized to distinguish 
spec-compliant future stubs from abandoned cruft.

## Categories

| Category | Count | Action |
|----------|-------|--------|
| SPEC_STUB | 18 | Keep - spec-compliant implementations for future integration |
| INTERNAL | 10 | Keep - used by internal subsystems |
| LEGACY | 6 | Document deprecation, consider removal |
| ACTIVE_STUB | 6 | Wire up when ready |

---

## MAYBE_UNUSED Items (Main Codebase)

### Parser/Parsing

| Function | File | Status | Notes |
|----------|------|--------|-------|
| ~~`parse_control_structure`~~ | src/parser.c:24 | **REMOVED** | Was forward declaration only, never implemented. Control structures handled via switch-case. Removed in commit 31f60f2. |

### Input Handling

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `is_terminator` | src/input.c:141 | SPEC_STUB | Checks for shell terminators (fi, done, esac). For multiline input handling. |
| `convert_multiline_for_history` | src/input.c:585 | SPEC_STUB | Converts multiline input to single-line for history. Planned feature. |

### Readline Integration

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `is_duplicate_in_cache` | src/readline_integration.c:306 | SPEC_STUB | History deduplication support. |
| `add_to_history_cache` | src/readline_integration.c:318 | SPEC_STUB | History caching with deduplication. |
| `lush_readline_with_layered_display` | src/readline_integration.c:1446 | SPEC_STUB | Enhanced readline with layered display. Graceful fallback. |
| ~~`try_layered_display_prompt`~~ | src/readline_integration.c | **REMOVED** | Superseded by new prompt approach. Removed Session 58. |

### Display System

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `display_output_buffer` | src/display_integration.c:148 | SPEC_STUB | Reserved for future batch output optimization. |
| `calculate_sequence_hash` | src/display/terminal_control.c:925 | SPEC_STUB | ANSI sequence caching optimization. |
| `find_cached_sequence` | src/display/terminal_control.c:943 | SPEC_STUB | Sequence cache lookup. |
| `cache_sequence` | src/display/terminal_control.c:956 | SPEC_STUB | Sequence caching storage. |
| `dc_get_timestamp_us` | src/display/display_controller.c:99 | SPEC_STUB | Performance timing (Spec 14). |
| ~~`dc_invalidate_prompt_cache`~~ | src/display/display_controller.c | **REMOVED** | No-op. Removed Session 58. |
| `calculate_cache_hash` | src/display/autosuggestions_layer.c:112 | SPEC_STUB | Autosuggestion cache optimization. |
| `expire_old_cache_entries` | src/display/command_layer.c:884 | SPEC_STUB | Syntax highlighting cache TTL. |
| `translate_cursor_to_screen_position` | src/display/composition_engine.c:1910 | SPEC_STUB | Multiline cursor positioning. |

### History

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `posix_history_entry_destroy` | src/posix_history.c:118 | INTERNAL | Cleanup function for history entries. |

### Arithmetic

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `push_numstackv` | src/arithmetic.c:573 | INTERNAL | Wrapper around `push_numstackv_with_context`. |
| `get_var_name` | src/arithmetic.c:711 | INTERNAL | Wrapper around `get_var_name_with_context`. |

### Executor

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `execute_external_command` | src/executor.c:2032 | INTERNAL | Wrapper around `execute_external_command_with_redirection`. |
| `execute_test_builtin` | src/executor.c:2643 | ACTIVE_STUB | Reserved for executor-aware test evaluation. |
| `copy_function_definitions` | src/executor.c:4785 | SPEC_STUB | Function context isolation in subshells. |
| `create_process` | src/executor.c:5253 | SPEC_STUB | Job control process structure creation. |

---

## LLE_MAYBE_UNUSED Items (LLE Subsystem)

### Terminal/Client Integration

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `convert_to_lush_format` | src/lle/terminal/terminal_lush_client.c:88 | ACTIVE_STUB | Awaiting display system integration (Spec 8, 13). |

### Keybinding Actions

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `is_word_codepoint` | src/lle/keybinding/keybinding_actions.c:70 | SPEC_STUB | Unicode-aware word boundary detection (Spec 25). |
| ~~`is_word_boundary`~~ | src/lle/keybinding/keybinding_actions.c | **REMOVED** | Byte-based legacy. Unicode version exists. Removed Session 58. |

### Display Cache

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `lle_lru_touch_entry` | src/lle/display/render_cache.c:254 | SPEC_STUB | LRU cache management. |
| `lle_lru_get_eviction_candidate` | src/lle/display/render_cache.c:273 | SPEC_STUB | LRU eviction policy. |

### Terminal Abstraction (Spec 2)

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `lle_terminal_state_update_dimensions` | src/lle/adaptive/adaptive_native_controller.c:161 | SPEC_STUB | Terminal resize detection. |
| `lle_native_move_cursor` | src/lle/adaptive/adaptive_native_controller.c:325 | SPEC_STUB | Cursor positioning via ANSI. |
| `lle_native_clear_screen` | src/lle/adaptive/adaptive_native_controller.c:349 | SPEC_STUB | Screen clearing. |
| `lle_native_clear_to_eol` | src/lle/adaptive/adaptive_native_controller.c:364 | SPEC_STUB | Clear to end of line. |
| `lle_native_show_cursor` | src/lle/adaptive/adaptive_native_controller.c:420 | SPEC_STUB | Show cursor. |
| `lle_native_hide_cursor` | src/lle/adaptive/adaptive_native_controller.c:433 | SPEC_STUB | Hide cursor (flicker prevention). |

### Minimal Terminal Adapter

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `lle_text_buffer_clear` | src/lle/adaptive/adaptive_minimal_controller.c:137 | SPEC_STUB | Buffer reset for minimal mode. |
| `lle_simple_completion_clear` | src/lle/adaptive/adaptive_minimal_controller.c:310 | SPEC_STUB | Completion cleanup (Spec 12). |

### History Core (Spec 9, 22)

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `get_entry_by_index_unlocked` | src/lle/history/history_core.c:636 | INTERNAL | Lock-free version for dedup. Caller must hold lock. |
| `get_entry_count_unlocked` | src/lle/history/history_core.c:773 | INTERNAL | Lock-free version. Caller must hold lock. |

### Testing Framework (Spec 17)

| Function | File | Status | Notes |
|----------|------|--------|-------|
| `test_action_error` | tests/lle/unit/test_keybinding.c:54 | INTERNAL | Test fixture for error handling tests. |
| `test_filter_configurable` | tests/lle/unit/test_event_phase2.c:67 | INTERNAL | Configurable test filter fixture. |

---

## Portable MAYBE_UNUSED Placement Rule

GCC requires `__attribute__((unused))` before the return type. Clang is lenient.

**Correct (portable):**
```c
MAYBE_UNUSED static int func(void) { }      // OK
static MAYBE_UNUSED int func(void) { }      // OK
MAYBE_UNUSED static int *func(void) { }     // OK
static MAYBE_UNUSED int *func(void) { }     // OK
```

**Incorrect (fails on GCC):**
```c
static int * MAYBE_UNUSED func(void) { }    // FAILS
static int *
MAYBE_UNUSED
func(void) { }                               // FAILS
```

---

## Recommendations

### Immediate Actions Taken (Session 58)
1. Fixed `lle_lru_get_eviction_candidate` placement (commit e0691a1)
2. Removed `parse_control_structure` dead declaration (commit 31f60f2)
3. Removed `global_config` unused extern (commit 31f60f2)

### Future Considerations

**LEGACY items - REMOVED in Session 58:**
- ~~`try_layered_display_prompt`~~ - Superseded, removed
- ~~`dc_invalidate_prompt_cache`~~ - No-op, removed
- ~~`is_word_boundary`~~ - Byte-based legacy, removed (Unicode version exists)

**ACTIVE_STUB items to wire up when ready:**
- `execute_test_builtin` - Enhanced test builtin evaluation
- `convert_to_lush_format` - Display system integration

---

## Audit Methodology

1. Searched for all `MAYBE_UNUSED` and `LLE_MAYBE_UNUSED` usages
2. Read function comments and surrounding code
3. Cross-referenced against LLE specs in `docs/lle_specification/`
4. Categorized based on documentation, implementation completeness, and spec alignment
