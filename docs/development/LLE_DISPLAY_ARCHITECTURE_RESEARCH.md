# LLE-Display Architecture Research
**Date**: 2025-11-17
**Purpose**: Comprehensive understanding before Phase 5 implementation
**Context**: Must understand existing architecture before adding completion menu rendering

**Reading note (2026-05-23):** the differential-update / diff-based
render path described below was **replaced**. The current
implementation is prompt-once + clear-and-redraw; `screen_buffer`
remains the cursor-math source of truth but `display_controller` is
the sole stdout writer for the REPL and does not call any
`screen_buffer_diff` / `screen_buffer_apply_diff` path. The
authoritative account is [`SCREEN_BUFFER_SPECIFICATION.md`](SCREEN_BUFFER_SPECIFICATION.md).
This research note is preserved as historical context for why the
data-flow shape ended up where it did; treat any "diff" references
as anachronisms.

---

## Executive Summary

**CRITICAL FINDING**: LLE does NOT currently use the display_controller system directly. The display integration happens through a different path than initially assumed. The "autosuggestions_layer pattern" reference was incorrect and potentially dangerous.

---

## 1. The Actual Data Flow (Source of Truth)

### Current Working Architecture

```
LLE Buffer (source of truth)
    v
GNU Readline Integration OR LLE Direct
    v
command_layer.c (receives raw command text + cursor position)
    v
command_layer applies syntax highlighting
    v
display_controller.c queries command_layer for highlighted text
    v
display_controller.c calls screen_buffer_render(prompt, highlighted_command, cursor_offset)
    v
screen_buffer creates virtual screen representation
    v
screen_buffer_diff() compares old vs new virtual screens
    v
screen_buffer_apply_diff() writes minimal ANSI sequences to terminal
    v
Terminal Display
```

### Key Insight: LLE is the Single Source of Truth

From user statement: **"LLE is the single source of truth for all things that eventually get rendered by the display"**

This means:
- LLE owns the buffer content
- LLE owns the cursor position
- Display system QUERIES LLE for current state
- Display system compares with previous state (diff tracking)
- Display system outputs minimal updates to terminal

---

## 2. Screen Buffer System (The Diff Tracking Mechanism)

### Located at: `src/display/screen_buffer.c/h`

### Core Concept

The screen buffer system implements a **virtual screen abstraction** used by ZLE, Fish, and Replxx:

1. **Maintain virtual representation** of terminal screen
2. **Render new state** into new virtual screen
3. **Diff old vs new** to find changes
4. **Generate minimal escape sequences** to apply changes

### Key Structures

**screen_cell_t**:
```c
typedef struct {
    char utf8_bytes[4];   // Full UTF-8 sequence (1-4 bytes)
    uint8_t byte_len;     // Actual bytes used
    uint8_t visual_width; // Display width (0, 1, or 2 columns)
    bool is_prompt;       // True if part of prompt
} screen_cell_t;
```

**screen_line_prefix_t** (Critical for completion menus):
```c
typedef struct {
    char *text;              // Prefix text (e.g., continuation prompt)
    size_t length;           // Length in bytes
    size_t visual_width;     // Visual width in columns
    bool contains_ansi;      // Has ANSI codes
    bool dirty;              // Changed since last render
} screen_line_prefix_t;
```

**screen_buffer_t**:
```c
typedef struct {
    screen_line_t lines[SCREEN_BUFFER_MAX_ROWS];
    int num_rows;
    int terminal_width;
    int cursor_row;          // ABSOLUTE screen coordinates
    int cursor_col;          // ABSOLUTE screen coordinates
    int command_start_row;   // Where command starts after prompt
    int command_start_col;
} screen_buffer_t;
```

### The Render Function

**`screen_buffer_render(buffer, prompt_text, command_text, cursor_byte_offset)`**

Location: `src/display/screen_buffer.c:300-450`

What it does:
1. Clears buffer
2. Renders prompt into virtual cells (skips ANSI codes, handles UTF-8, tracks wrapping)
3. Saves `command_start_row` and `command_start_col`
4. Renders command text (skips ANSI codes, handles UTF-8, tracks wrapping)
5. **CRITICAL**: After newlines, checks for line prefixes and adjusts column position:
   ```c
   // Line 359-363
   if (ch == '\n') {
       row++;
       // Check if next row has continuation prompt prefix
       size_t prefix_width = screen_buffer_get_line_prefix_visual_width(buffer, row);
       col = (int)prefix_width;  // Start AFTER prefix
   }
   ```
6. Tracks cursor position by counting bytes processed (excluding ANSI)
7. Returns virtual screen with absolute cursor coordinates

### The Diff Function

**`screen_buffer_diff(old_buffer, new_buffer, diff)`**

Location: `src/display/screen_buffer.c:450-550`

What it does:
1. Compares line by line
2. For each line, finds first and last difference
3. Generates change operations:
   - `SCREEN_CHANGE_WRITE_TEXT` - write text at position
   - `SCREEN_CHANGE_CLEAR_TO_EOL` - clear to end of line
   - `SCREEN_CHANGE_CLEAR_TO_EOS` - clear to end of screen
   - `SCREEN_CHANGE_MOVE_CURSOR` - move cursor to position
4. Returns minimal set of changes needed

### The Apply Function

**`screen_buffer_apply_diff(diff, fd)`**

Location: `src/display/screen_buffer.c:550-620`

What it does:
1. For each change, write ANSI escape sequence to terminal:
   - `\033[row;colH` - move to position (1-indexed)
   - Write text
   - `\033[K` - clear to end of line
   - `\033[J` - clear to end of screen
2. Calls `fsync(fd)` to ensure output

---

## 3. Display Controller Orchestration

### Located at: `src/display/display_controller.c`

### The Render Cycle

**Entry Point**: `display_controller_display_with_cursor()`

**What it does** (Line 200-320):

1. **Get prompt text** from prompt_layer
2. **Get highlighted command text** from command_layer:
   ```c
   command_layer_get_highlighted_text(cmd_layer, command_buffer, sizeof(command_buffer));
   ```
3. **Detect multiline** and set continuation prompt prefixes on screen_buffer
4. **Get cursor offset** from command_layer: `cmd_layer->cursor_position`
5. **Render to virtual screen**:
   ```c
   screen_buffer_render(&desired_screen, prompt_buffer, command_buffer, cursor_byte_offset);
   ```
6. **Diff against old screen**:
   ```c
   screen_buffer_diff(&last_screen, &desired_screen, &diff);
   ```
7. **Apply diff to terminal**:
   ```c
   screen_buffer_apply_diff(&diff, STDOUT_FILENO);
   ```
8. **Save desired as new last_screen** for next cycle

### PROMPT-ONCE Architecture

From Line 312-320:
```c
/* PROMPT-ONCE ARCHITECTURE per MODERN_EDITOR_WRAPPING_RESEARCH.md
 * 
 * This implements the proven approach used by Replxx, Fish, and ZLE:
 * 1. Prompt is drawn ONCE on first render and NEVER redrawn
 * 2. All subsequent edits only update command portion via diff
 * 3. Wrapping handled by screen_buffer with proper line tracking
 * 
 * This prevents ALL prompt corruption and wrapping bugs.
 */
```

---

## 4. Command Layer (Where LLE State Enters Display System)

### Located at: `include/display/command_layer.h`, `src/display/command_layer.c`

### Key Structure

```c
typedef struct {
    char *command_text;                    // Raw command text
    size_t command_length;                 // Text length
    char *highlighted_text;                // Syntax highlighted version
    size_t highlighted_length;
    size_t cursor_position;                // Cursor byte offset
    // ... syntax highlighting, caching, performance tracking
} command_layer_t;
```

### Key Functions

**`command_layer_update_command(layer, new_command, cursor_pos)`**
- Sets the raw command text
- Sets cursor position
- Triggers syntax highlighting
- Fires LAYER_EVENT_COMMAND_TEXT_CHANGED

**`command_layer_get_highlighted_text(layer, output, size)`**
- Returns syntax-highlighted version of command text
- Used by display_controller

### How Data Flows In

The command_layer receives:
- Raw command text (from GNU Readline or LLE)
- Cursor position (byte offset in raw text)

It provides:
- Syntax-highlighted text (with ANSI codes)
- Cursor position (unchanged, byte offset)

---

## 5. Continuation Prompt Integration (Working Example)

### Located at: `src/display/display_controller.c:220-310`

### How It Works

**Step 1**: Detect multiline input
```c
int newline_count = count_newlines(command_buffer);
bool is_multiline = (newline_count > 0);
```

**Step 2**: If multiline, analyze each line for shell state
```c
continuation_state_t cont_state;
continuation_state_init(&cont_state);
continuation_state_analyze_line_by_line(&cont_state, command_buffer_no_ansi);
```

**Step 3**: Set line prefixes on screen_buffer BEFORE rendering
```c
for (int line_idx = 0; line_idx < cont_state.num_lines; line_idx++) {
    const char *prompt = continuation_state_get_prompt_for_line(&cont_state, line_idx);
    if (prompt && line_idx > 0) {  // Skip first line (has main prompt)
        screen_buffer_set_line_prefix(&desired_screen, line_idx, prompt);
    }
}
```

**Step 4**: Render - screen_buffer_render accounts for prefix widths
```c
screen_buffer_render(&desired_screen, prompt_buffer, command_buffer, cursor_byte_offset);
```

**Step 5**: Diff and apply as normal

### Key Insight

Continuation prompts are:
- Set as **line prefixes** on screen_buffer
- **NOT rendered into command text**
- **Tracked separately** with independent dirty flags
- **Accounted for** in cursor position calculations

---

## 6. What Does NOT Exist / Is NOT Used

### Autosuggestions Layer

- **File exists**: `include/display/autosuggestions_layer.h`
- **Status**: NOT integrated, NOT used
- **Why**: Previous attempts failed for architectural reasons
- **Conclusion**: Do NOT use as pattern

### LLE Direct Display Integration

From `src/lle/terminal_lush_client.c:122-132`:
```c
/* For now, use simple direct rendering until display_controller integration is complete.
 * 
 * The proper architecture (to be implemented):
 * 1. Convert LLE display_content to display_controller format
 * 2. Call display_controller_display(controller, prompt, command, output, size)
 * 3. Write the composed output to stdout
 * 
 * Current temporary implementation writes directly to terminal.
 */
```

**Conclusion**: LLE currently does NOT use display_controller when running standalone.

---

## 7. The Sacred Flow (With Evidence)

### Actual Working Flow

```
[LLE Buffer - Source of Truth]
       v
  command_layer_update_command(text, cursor)
       v
  command_layer applies syntax highlighting
       v
  display_controller queries:
    - prompt_layer_get_rendered_content()
    - command_layer_get_highlighted_text()
    - cmd_layer->cursor_position
       v
  display_controller detects multiline
  and sets continuation prompt prefixes
       v
  screen_buffer_render(prompt, command, cursor_offset)
    [Creates virtual screen, accounts for prefixes]
       v
  screen_buffer_diff(old, new)
    [Compares virtual screens cell-by-cell]
       v
  screen_buffer_apply_diff(diff, STDOUT)
    [Writes minimal ANSI sequences]
       v
  [Terminal Display]
```

### What Must NOT Be Violated

1. **LLE is source of truth** - display queries LLE, never modifies LLE state
2. **Diff tracking** - all updates go through screen_buffer diff system
3. **No direct terminal I/O** from LLE or completion system
4. **Prefix system** - use screen_buffer line prefixes, don't render into cells
5. **Cursor tracking** - absolute screen coordinates, calculated by screen_buffer_render
6. **ANSI skipping** - screen_buffer correctly skips ANSI when counting bytes for cursor

---

## 8. Critical Questions for Phase 5

### Where Does Completion Menu State Live?

**Answer**: In LLE completion menu state (Phase 4 complete)

### How Does Display System Learn About Menu?

**Options**:

**Option A**: Extend command_layer to query completion menu state
- command_layer already has cursor position
- Add: `command_layer_get_completion_menu(layer, output, size)`
- Returns formatted menu text or NULL

**Option B**: Add completion_layer parallel to command_layer
- New layer in composition engine
- Queries LLE completion state
- Renders menu independently

**Option C**: Render menu as "prefix" or "suffix" on lines
- Use screen_buffer line prefix system
- OR add line suffix system

### How Does Menu Text Get Into screen_buffer?

**Answer**: Through screen_buffer_render() like everything else

**Question**: How?

1. Append menu to command text?
2. Use line prefixes/suffixes?
3. Render menu separately then compose?

### When Does Menu Render?

**Trigger**: When completion menu state changes (menu_active = true)

**Flow**:
1. LLE completion generates results
2. LLE menu state becomes active
3. Display cycle detects menu active
4. Menu text generated from menu state
5. Menu text added to screen_buffer (HOW?)
6. Diff/apply as normal

### How Does Cursor Position Work With Menu?

**Critical**: Cursor must stay in command text, not jump to menu

**Solution**: Menu is separate from cursor position
- Command cursor unchanged
- Menu appears below/above command
- Menu has its own "selection highlight" (not cursor)

---

## 9. Next Steps Before Implementation

1. **Read continuation prompt code completely** - understand exact integration pattern
2. **Trace complete render cycle** with debugger or detailed code reading
3. **Understand command_layer lifecycle** - when/how it's updated
4. **Determine menu rendering approach** - which option above
5. **Document exact integration points** with line numbers and function calls
6. **Create Phase 5 implementation plan** with specific code changes

---

## 10. Warnings and Red Flags

### Do NOT:
- Reference autosuggestions_layer as pattern (it's broken/unused)
- Assume LLE uses display_controller directly (it doesn't currently)
- Bypass screen_buffer diff system
- Modify LLE state from display system
- Add terminal I/O to completion code
- Render menu text into command text buffer directly

### DO:
- Follow continuation prompt pattern (working example)
- Use screen_buffer line prefix/suffix system if possible
- Maintain LLE as source of truth
- Query completion state, don't push
- Let screen_buffer handle all cursor calculations
- Use diff tracking for all updates

---

## 11. Open Questions

1. How exactly does command_layer receive updates? Event-driven or polling?
2. Where is the render cycle triggered from? Main loop?
3. How does continuation prompt detect shell state? (need to read that code)
4. Can screen_buffer handle menus at current cursor position or only at line boundaries?
5. What's the maximum practical size for completion menu before performance issues?
6. How to handle menu scrolling within screen_buffer paradigm?

---

## Status

- [x] Understand screen_buffer diff tracking mechanism
- [x] Understand display_controller orchestration
- [x] Identify actual data flow from LLE to terminal
- [x] Identify working integration example (continuation prompts)
- [ ] Read continuation prompt implementation completely
- [ ] Trace full render cycle with exact function calls
- [ ] Determine menu rendering approach
- [ ] Create detailed Phase 5 implementation plan
