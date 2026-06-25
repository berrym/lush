# Screen Buffer Menu Integration - Implementation Plan

**Date**: 2025-11-20
**Purpose**: Integrate completion menu rendering with screen_buffer to fix all display issues
**Based on**: Continuation prompt success pattern + Claude Opus analysis

**Status**: IMPLEMENTED (2025-12-03)

> **RESTORED 2026-06-24** — removed in the `856e8f79a` doc sweep, but the live
> code cites it by name (`display_controller.c:566`, `:888`). Its central design
> is implemented and current, so it is restored.
>
> **Still accurate and load-bearing:** the **"menu rows are just more content
> rows"** insight. Menu (and notification) rows are appended to the virtual
> screen via `screen_buffer_add_text_rows` (`display_controller.c:~580`), which
> extends `num_rows`, so `screen_buffer_get_rows_below_cursor()` makes cursor
> positioning correct automatically. This is the proven pattern to follow for any
> new overlay.
>
> **Status of the three fields this plan added to `screen_buffer_t`
> (verified 2026-06-24):**
> - `menu_lines` — populated by `add_text_rows`, but **overwritten per call**, so
>   it holds only the last overlay's count; the cursor math does not depend on it
>   (it uses cumulative `num_rows`).
> - `total_display_rows` — set to `num_rows` (not a separate sum).
> - **`ghost_text_lines` — still DEAD/RESERVED**: only ever set to 0, never read.
>   Autosuggestion ghost rows are NOT yet routed through this unified geometry —
>   they are computed by an ad-hoc `ghost_text_extra_rows` side-calc in
>   `dc_handle_redraw_needed`. Finishing the ghost half (a `screen_buffer_add_
>   ghost_text` that populates `ghost_text_lines`, noting the ghost begins INLINE
>   on the command's last row) is the open follow-up the `display_controller.c:903`
>   TODO refers to.
>
> **Write-model caveat:** the terminal is written via many small EINTR-safe
> `dc_write_all` calls, not one accumulated write; `screen_buffer` is geometry
> only. The differential/diff path mentioned in sibling docs was removed
> (`bddfac9ad`).

---

## Implementation Summary

This plan was successfully implemented on 2025-12-03. The key insight was correct:
**menu rows are just more content rows** - by adding them to screen_buffer, cursor
positioning calculations become correct automatically.

### What Was Implemented

1. **Extended `screen_buffer_t`** (`include/display/screen_buffer.h`):
   - Added `menu_lines`, `ghost_text_lines`, `total_display_rows` fields
   - Added `command_end_row`, `command_end_col` to track where command ends

2. **Added `screen_buffer_add_text_rows()`** (`src/display/screen_buffer_menu.c`):
   - Adds menu text as additional rows to screen_buffer AFTER command rendering
   - Properly handles ANSI escape codes (skips them in width calculation)
   - Handles UTF-8 and wide characters (CJK, emoji)
   - Updates `num_rows` to include menu rows

3. **Added `screen_buffer_get_rows_below_cursor()`** (`src/display/screen_buffer_menu.c`):
   - Returns `(num_rows - 1) - cursor_row`
   - Used by display_controller for cursor movement calculation

4. **Modified `dc_handle_redraw_needed()`** (`src/display/display_controller.c`):
   - Calls `screen_buffer_add_text_rows()` after `screen_buffer_render()`
   - Uses `screen_buffer_get_rows_below_cursor()` for cursor positioning
   - Simplified Step 5 cursor movement logic

### Why Differential Updates Were Not Used

The original plan suggested using `screen_buffer_diff()` and `screen_buffer_apply_diff()`
for efficient updates. This was NOT implemented because:

1. The current diff implementation has known issues and is not production-ready
2. The simple "clear to end of screen and redraw" approach works reliably
3. Adding menu rows to screen_buffer fixes cursor positioning regardless of
   whether differential or full redraw is used

The differential update system can be revisited later as a performance optimization,
but the core fix (tracking menu rows in screen_buffer) is independent of that.

### Testing Results

Tested on macOS with various scenarios:
- Menu display at top, middle, and bottom of terminal
- Arrow key navigation through menu items
- TAB cycling through completions
- Terminal scrolling behavior
- Multi-line commands with menu

All tests passed without display glitches.

---

## Original Analysis (Historical Reference)

## Executive Summary

The completion menu currently bypasses screen_buffer with direct `write()` calls, causing:
- Menu corruption during TAB cycling
- Menu never clearing properly
- Cursor position desync
- Line wrapping issues

**Solution**: Add menu rows to screen_buffer as additional content rows AFTER the command, following the same pattern that makes continuation prompts work.

---

## Current Architecture Analysis

### How screen_buffer_render() Works

After analyzing the code, here's how `num_rows` is managed:

```c
// screen_buffer.c - screen_buffer_render()

int row = 0;  // Start at row 0
int col = 0;

// 1. Render prompt (may span multiple rows with newlines/wrapping)
while (rendering_prompt) {
    if (newline || wrapping) {
        row++;
        col = 0;
        if (row >= buffer->num_rows) {
            buffer->num_rows = row + 1;  // Expand as needed
        }
    }
}

// 2. Mark where command starts
buffer->command_start_row = row;
buffer->command_start_col = col;

// 3. Render command (may span multiple rows)
while (rendering_command) {
    if (newline) {
        row++;
        // Account for continuation prompt width
        size_t prefix_width = screen_buffer_get_line_prefix_visual_width(buffer, row);
        col = (int)prefix_width;
        
        if (row >= buffer->num_rows) {
            buffer->num_rows = row + 1;
        }
    }
    if (wrapping) {
        row++;
        col = 0;
        if (row >= buffer->num_rows) {
            buffer->num_rows = row + 1;
        }
    }
}

// 4. Final state
buffer->num_rows = (actual rows occupied);  // Could be 1, 5, 20, etc.
buffer->cursor_row = (where cursor is in command);
buffer->cursor_col = (column position accounting for prefixes/wrapping);
```

**Key Insight**: `num_rows` accurately represents total rows occupied by prompt + command, accounting for:
- Multi-line prompts
- Line wrapping (both in prompt and command)
- Continuation prompts (tracked separately as prefixes)
- Multi-line input mode

### Current Broken Menu Rendering (display_controller.c:439-442)

```c
/* Step 4b: Write completion menu WITHOUT continuation prompts */
if (menu_text && *menu_text) {
    write(STDOUT_FILENO, "\n", 1);           // DIRECT WRITE - BAD!
    write(STDOUT_FILENO, menu_text, strlen(menu_text));  // DIRECT WRITE - BAD!
}
```

**Problems**:
1. Bypasses screen_buffer's virtual screen tracking
2. `current_screen` doesn't know menu exists
3. When menu should disappear, diff system can't generate CLEAR_TO_EOS
4. Cursor calculations don't account for menu rows
5. Menu text overwrites itself on each cycle (no clearing)

---

## The Solution: Add Menu as Additional Rows

### Key Design Principles

1. **Menu rows are just more content rows** - They're not special metadata like prefixes
2. **Add them AFTER screen_buffer_render() completes** - Don't modify screen_buffer_render()
3. **Use existing screen_buffer APIs** - No new render functions needed
4. **Let differential system handle updates** - It already knows how to clear/update rows

### Architecture Flow

```
Current Flow (BROKEN):
┌─────────────────────────────────────────┐
│ 1. screen_buffer_render()              │
│    - Renders prompt + command          │
│    - Sets num_rows = 5 (example)       │
│    - Sets cursor_row, cursor_col       │
└─────────────────────────────────────────┘
         ↓
┌─────────────────────────────────────────┐
│ 2. Direct write() to terminal          │
│    - Bypasses everything               │
│    - Menu appears but not tracked      │
└─────────────────────────────────────────┘
         ↓
┌─────────────────────────────────────────┐
│ 3. screen_buffer_diff()                │
│    - Compares rows 0-4 only            │
│    - Doesn't know menu exists          │
│    - Can't generate clear commands     │
└─────────────────────────────────────────┘

Proper Flow (FIXED):
┌─────────────────────────────────────────┐
│ 1. screen_buffer_render()              │
│    - Renders prompt + command          │
│    - Sets num_rows = 5                 │
│    - Sets cursor_row, cursor_col       │
└─────────────────────────────────────────┘
         ↓
┌─────────────────────────────────────────┐
│ 2. Add menu rows to screen_buffer      │
│    - Parse menu text into rows         │
│    - Add each row starting at row 5    │
│    - Update num_rows = 5 + menu_rows   │
│    - Menu is now part of virtual screen│
└─────────────────────────────────────────┘
         ↓
┌─────────────────────────────────────────┐
│ 3. screen_buffer_diff()                │
│    - Compares ALL rows (0 to num_rows) │
│    - Knows menu exists                 │
│    - Generates proper clear/update     │
└─────────────────────────────────────────┘
```

---

## Implementation Steps

### Step 1: Create Helper Function to Add Raw Text Rows

We need a function to add plain text (menu) as new rows to screen_buffer.

**Why not use screen_buffer_render()?** 
- That function is designed for prompt+command with cursor tracking
- Menu is simpler: just add text rows with no cursor, no prefixes

**New function**: `screen_buffer_add_text_rows()`

```c
// screen_buffer.h - Add declaration

/**
 * Add plain text rows to screen buffer (for menu, hints, etc.)
 * 
 * Parses text line-by-line and adds each line as a new row starting
 * at the specified row. Updates buffer->num_rows to include new rows.
 * 
 * This is for adding content AFTER the main command text, like menus.
 * 
 * @param buffer Screen buffer to modify
 * @param start_row Row number to start adding (usually buffer->num_rows)
 * @param text Multi-line text to add (may contain \n, ANSI codes, UTF-8)
 * @return Number of rows added, or -1 on error
 */
int screen_buffer_add_text_rows(
    screen_buffer_t *buffer,
    int start_row,
    const char *text
);
```

**Implementation** (screen_buffer.c):

```c
int screen_buffer_add_text_rows(
    screen_buffer_t *buffer,
    int start_row,
    const char *text
) {
    if (!buffer || !text || start_row < 0 || start_row >= SCREEN_BUFFER_MAX_ROWS) {
        return -1;
    }
    
    int current_row = start_row;
    int col = 0;
    size_t i = 0;
    size_t text_len = strlen(text);
    int rows_added = 0;
    
    while (i < text_len && current_row < SCREEN_BUFFER_MAX_ROWS) {
        unsigned char ch = (unsigned char)text[i];
        
        // Handle ANSI escape sequences (skip, take 0 columns)
        if (ch == '\033' || ch == '\x1b') {
            i++;
            if (i < text_len && text[i] == '[') {
                i++;
                while (i < text_len) {
                    char c = text[i++];
                    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
                        c == 'm' || c == 'H' || c == 'J' || c == 'K' || c == 'G') {
                        break;
                    }
                }
            }
            continue;
        }
        
        // Handle newlines
        if (ch == '\n') {
            current_row++;
            col = 0;
            rows_added++;
            
            if (current_row >= buffer->num_rows) {
                buffer->num_rows = current_row + 1;
            }
            
            i++;
            continue;
        }
        
        // Decode UTF-8 character
        uint32_t codepoint;
        int char_bytes = lle_utf8_decode_codepoint(text + i, text_len - i, &codepoint);
        
        if (char_bytes > 0 && codepoint >= 32) {
            int visual_width = lle_utf8_codepoint_width(codepoint);
            
            if (visual_width > 0) {
                // Check for wrapping
                if (col >= buffer->terminal_width) {
                    current_row++;
                    col = 0;
                    rows_added++;
                    
                    if (current_row >= SCREEN_BUFFER_MAX_ROWS) {
                        break;
                    }
                    
                    if (current_row >= buffer->num_rows) {
                        buffer->num_rows = current_row + 1;
                    }
                }
                
                // Write character to buffer
                write_char_to_buffer(buffer, text + i, char_bytes, visual_width,
                                    false, &current_row, &col);
                
                // Handle wide characters
                if (visual_width == 2) {
                    col++;
                    if (col >= buffer->terminal_width) {
                        current_row++;
                        col = 0;
                        rows_added++;
                        
                        if (current_row >= buffer->num_rows) {
                            buffer->num_rows = current_row + 1;
                        }
                    }
                }
            }
            
            i += char_bytes;
        } else {
            i++;
        }
    }
    
    // Count the current row if we wrote anything to it
    if (col > 0) {
        rows_added++;
        if (current_row >= buffer->num_rows) {
            buffer->num_rows = current_row + 1;
        }
    }
    
    return rows_added;
}
```

### Step 2: Modify display_controller.c to Use screen_buffer

**Current location**: `src/display/display_controller.c:224-247`

**Change**:

```c
// BEFORE (lines 224-247): Render menu text into buffer
char menu_buffer[8192] = {0};
char *menu_text = NULL;

if (controller->completion_menu_visible && controller->active_completion_menu) {
    lle_menu_render_options_t options = lle_menu_renderer_default_options(term_width);
    options.max_rows = 20;
    
    lle_menu_render_stats_t stats;
    lle_result_t result = lle_completion_menu_render(
        controller->active_completion_menu,
        &options,
        menu_buffer,
        sizeof(menu_buffer),
        &stats
    );
    
    if (result == LLE_SUCCESS && menu_buffer[0]) {
        menu_text = menu_buffer;
    }
}

// ... continuation prompt setup ...

screen_buffer_render(&desired_screen, prompt_buffer, command_buffer, cursor_byte_offset);

// AFTER (NEW CODE): Add menu to screen_buffer
if (controller->completion_menu_visible && controller->active_completion_menu) {
    // Render menu into temporary buffer
    char menu_buffer[8192] = {0};
    lle_menu_render_options_t options = lle_menu_renderer_default_options(term_width);
    options.max_rows = 20;
    
    lle_menu_render_stats_t stats;
    lle_result_t result = lle_completion_menu_render(
        controller->active_completion_menu,
        &options,
        menu_buffer,
        sizeof(menu_buffer),
        &stats
    );
    
    if (result == LLE_SUCCESS && menu_buffer[0]) {
        // Add menu rows to screen_buffer starting after command
        int menu_start_row = desired_screen.num_rows;
        int rows_added = screen_buffer_add_text_rows(&desired_screen, menu_start_row, menu_buffer);
        
        DC_DEBUG("Added menu: start_row=%d, rows_added=%d, new_num_rows=%d",
                 menu_start_row, rows_added, desired_screen.num_rows);
    }
}
```

**Key points**:
1. Menu rendering still uses `lle_completion_menu_render()` (no changes there)
2. Menu text is added to screen_buffer AFTER `screen_buffer_render()`
3. Menu starts at `desired_screen.num_rows` (right after command ends)
4. `screen_buffer_add_text_rows()` updates `num_rows` automatically
5. Cursor position stays in command area (not affected by menu rows)

### Step 3: Remove Direct Terminal Writes

**Location**: `src/display/display_controller.c:439-442`

**Remove these lines**:
```c
/* Step 4b: Write completion menu WITHOUT continuation prompts */
if (menu_text && *menu_text) {
    write(STDOUT_FILENO, "\n", 1);
    write(STDOUT_FILENO, menu_text, strlen(menu_text));
}
```

**Why this works**: The menu is now part of `desired_screen`, so the existing code that uses screen_buffer_diff() and screen_buffer_apply_diff() will handle it automatically.

### Step 4: Remove Menu Line Counting Hack

**Location**: `src/display/display_controller.c:451-466`

**Remove these lines**:
```c
/* Count menu lines to adjust final cursor position */
int menu_lines = 0;
if (menu_text && *menu_text) {
    menu_lines = 1;  /* The newline before menu counts as one */
    for (const char *p = menu_text; *p; p++) {
        if (*p == '\n') {
            menu_lines++;
        }
    }
}
```

**Why remove**: The cursor position is calculated by screen_buffer_render() which doesn't include menu rows. Menu rows are added AFTER, so cursor calculations are already correct.

---

## How This Fixes All the Issues

### Issue 1: Menu Corruption During TAB Cycling

**Before**: Direct write() overwrites previous menu without clearing  
**After**: screen_buffer_diff() detects changed menu rows and generates proper update commands

```
Cycle 1: desired_screen has menu rows 5-10 with "lsar" highlighted
Cycle 2: desired_screen has menu rows 5-10 with "lsattr" highlighted
Diff: Rows 5-10 changed -> Generate WRITE_TEXT for those rows
Result: Menu updates cleanly, no corruption
```

### Issue 2: Menu Never Clears

**Before**: Direct write() adds menu, but nothing removes it  
**After**: When menu cleared, desired_screen.num_rows shrinks, diff generates CLEAR_TO_EOS

```
Menu active: desired_screen.num_rows = 15 (command + menu)
Menu cleared: desired_screen.num_rows = 5 (command only)
Diff: Rows 5-14 no longer exist -> Generate CLEAR_TO_EOS from row 5
Result: Menu disappears properly
```

### Issue 3: Cursor Position Wrong

**Before**: Cursor calc didn't account for menu rows, manual hack tried to fix  
**After**: Cursor position is in command area (rows 0-4), menu rows (5+) don't affect it

```
Command occupies rows 0-4
Cursor at row 3, col 10
Menu occupies rows 5-10

screen_buffer_render() sets:
  cursor_row = 3  (in command)
  cursor_col = 10

screen_buffer_add_text_rows() sets:
  num_rows = 11  (includes menu)

Cursor positioned to row 3, col 10 -> Correct!
```

### Issue 4: Backspace/ESC Don't Clear Menu

**Before**: No mechanism to detect input and clear menu  
**After**: When input happens, completion system is cleared, menu not added to desired_screen, diff clears it

```
Input handlers call: lle_completion_system_clear()
Next render: Menu not added to desired_screen
Diff: Old screen had menu, new doesn't -> CLEAR_TO_EOS
Result: Menu disappears on backspace/ESC
```

---

## Testing Plan

### Test 1: Basic Menu Display
```bash
lusush$ ls<TAB>
Expected: Menu appears below command, clean rendering
Verify: Check num_rows increased, menu rows present in screen_buffer
```

### Test 2: TAB Cycling
```bash
lusush$ ls<TAB><TAB><TAB>
Expected: Menu highlight cycles, no corruption
Verify: Each cycle updates menu rows cleanly via diff
```

### Test 3: Menu Dismissal
```bash
lusush$ ls<TAB><BACKSPACE>
Expected: Menu disappears
Verify: desired_screen.num_rows shrinks, CLEAR_TO_EOS generated

lusush$ ls<TAB><ESC>
Expected: Menu disappears
Verify: Same as backspace

lusush$ ls<TAB><Ctrl+G>
Expected: Command cleared, menu disappears, new prompt
Verify: Both command and menu cleared
```

### Test 4: Character Insertion
```bash
lusush$ ls<TAB>x
Expected: Menu disappears, "x" inserted after "lsar"
Verify: Menu not added to desired_screen on next render
```

### Test 5: Multi-line Command with Menu
```bash
lusush$ for i in 1 2 3; do
loop> ec<TAB>
Expected: Menu appears below continuation prompt line
Verify: Menu starts at correct row accounting for wrapped lines
```

### Test 6: Line Wrapping with Menu
```bash
# Terminal width 80, type long command
lusush$ very_long_command_name_that_will_wrap_to_next_line<TAB>
Expected: Menu appears after wrapped command line
Verify: num_rows accounts for wrapping, menu positioned correctly
```

---

## Potential Issues and Mitigations

### Issue: write_char_to_buffer() is static

**Problem**: `write_char_to_buffer()` is static in screen_buffer.c, can't be called from new function  
**Solution**: Make it non-static or duplicate the logic (it's simple)

### Issue: Menu text too long

**Problem**: Menu might exceed SCREEN_BUFFER_MAX_ROWS  
**Mitigation**: Already handled - `lle_completion_menu_render()` has max_rows limit (20)

### Issue: UTF-8/ANSI in menu

**Problem**: Menu text contains ANSI color codes and UTF-8  
**Mitigation**: `screen_buffer_add_text_rows()` handles both (same logic as screen_buffer_render)

---

## Success Criteria

1. ✅ Menu renders without corruption
2. ✅ TAB cycling updates menu cleanly
3. ✅ Backspace clears menu
4. ✅ ESC clears menu
5. ✅ Ctrl+G clears menu
6. ✅ Character insertion clears menu
7. ✅ Enter accepts completion (separate fix)
8. ✅ Cursor position stays correct
9. ✅ Works with multi-line input
10. ✅ Works with line wrapping

---

## Files to Modify

1. **include/display/screen_buffer.h** - Add `screen_buffer_add_text_rows()` declaration
2. **src/display/screen_buffer.c** - Implement `screen_buffer_add_text_rows()`
3. **src/display/display_controller.c** - Use screen_buffer, remove direct writes

**Estimated changes**: ~150 lines added, ~30 lines removed

---

## Conclusion

This approach follows the proven continuation prompt pattern:
- Metadata added to screen_buffer before/after main render
- Differential system handles all updates automatically
- Clean separation of concerns

The key insight: **Menu rows are just more content**. They don't need special treatment - just add them to the virtual screen and let the existing machinery work.

This is simpler than Opus's suggestion to create `screen_buffer_render_with_menu()` and more consistent with the existing architecture.
