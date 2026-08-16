# Research: Modern Line Editor Differential Display Mechanisms

## Overview

This document details the exact proven mechanisms used by ZSH/ZLE, Fish, and Replxx for differential display updates. The goal is to understand how these editors track internal state, use terminal escape sequences, and integrate with their display systems--then determine how this applies to Lush LLE.

**Created**: 2026-01-01 (Session 95)
**Purpose**: Research reference for future differential display work
**Status**: Complete research, no implementation planned

---

## Executive Summary

All three editors (ZLE, Fish, Replxx) use the same fundamental pattern:

1. **Dual Virtual Screen Buffers**: Maintain `current` (what's displayed) and `desired` (what should be displayed)
2. **Line-by-Line Diff**: Compare buffers to identify changed rows
3. **Relative Cursor Movement**: Use `\033[nA` (up), `\033[nB` (down), `\033[nG` (absolute column)
4. **Prompt-Once Architecture**: Draw prompt once, never redraw
5. **No Terminal Queries**: Never ask terminal for cursor position--internal state is authoritative

---

## 1. ZSH ZLE (zle_refresh.c)

### State Tracking

ZLE maintains dual video buffers:
- **`nbuf`** (new buffer): Target display state
- **`obuf`** (old buffer): Currently displayed state

Buffers are swapped after each refresh cycle rather than freed/allocated.

### Data Structures

```c
/* REFRESH_ELEMENT - single display cell */
typedef struct {
    wchar_t chr;    /* Character to display */
    int atr;        /* Attributes (color, bold, etc.) */
} REFRESH_ELEMENT;

/* Multiword buffers for combining characters */
wchar_t *nmwbuf;    /* New multiword glyph buffer */
wchar_t *omwbuf;    /* Old multiword glyph buffer */
```

### Escape Sequences

| Capability | Sequence | Purpose |
|------------|----------|---------|
| `TCMULTRIGHT` | `\033[nC` | Move right n columns |
| `TCHORIZPOS` | `\033[nG` | Absolute column (1-based) |
| `TCUP/TCMULTUP` | `\033[nA` | Move up n rows |
| `TCDOWN/TCMULTDOWN` | `\033[nB` | Move down n rows |
| `TCLEFT/TCMULTLEFT` | `\033[nD` | Move left n columns |
| `TCCLEAREOL` | `\033[K` | Clear to end of line |
| `TCCLEAREOD` | `\033[J` | Clear to end of display |

### Core Algorithm: `zrefresh()`

```
1. Build new video buffer (nbuf) from:
   - Prompt text
   - Command buffer with syntax highlighting
   - Status line, completion list if visible
   
2. Compare nbuf vs obuf line-by-line:
   - Find first differing character per line
   - Use delete/insert operations if cost-effective
   - Otherwise overwrite changed portion
   
3. Cursor movement via moveto():
   - Chooses cheapest movement option
   - Considers horizontal vs absolute positioning
   - Handles line wrapping edge cases
   
4. Swap buffers: obuf = nbuf
```

### Line Wrapping

- `nextline()` function detects overflow when column >= `winw` (window width)
- `scrollwindow()` handles vertical scrolling
- Ellipsis markers (`<....`, `>....`) for clipped content

---

## 2. Fish Shell (screen.rs)

### State Tracking

Fish maintains two `ScreenData` structures:
- **`desired`**: Target screen computed from prompt + command + pager
- **`actual`**: Current screen, updated after rendering

### Data Structures

```rust
/// Single character with highlighting
struct HighlightedChar {
    character: char,
    highlight: HighlightSpec,
    source_offset: usize,  // Position in logical command line
}

/// One line of display
struct Line {
    text: Vec<HighlightedChar>,
    indentation: usize,
    is_soft_wrapped: bool,
}

/// Complete screen state
struct ScreenData {
    lines: Vec<Line>,
    cursor: Cursor,
    // ...
}

struct Screen {
    desired: ScreenData,
    actual: ScreenData,
    // ...
}
```

### Escape Sequences

Fish uses abstracted terminal commands:
- `CursorUp`, `CursorDown`, `CursorLeft`, `CursorRight`
- `CursorMove` (absolute positioning)
- `ClearToEndOfLine`, `ClearToEndOfScreen`
- `EnterDimMode`, `ExitAttributeMode`
- `ScrollContentUp`

### Core Algorithm: `update()`

```rust
fn update(&mut self) {
    // 1. Compare cached prompt layout - repaint only if changed
    
    // 2. For each line in desired:
    //    - Compute shared prefix with actual line using line_shared_prefix()
    //    - Skip unchanged prefix region
    //    - Output only divergent content
    
    // 3. Selective clearing:
    //    - ClearToEndOfLine only when previous was wider
    //    - ClearToEndOfScreen only when lines removed
    
    // 4. Soft wrap optimization:
    //    - Preserve wrapped lines if wrapping unchanged
    
    // 5. Clone desired into actual
    self.actual = self.desired.clone();
}
```

### Key Insight: `line_shared_prefix()`

Fish identifies matching text AND colors at line starts, allowing it to skip outputting unchanged prefix regions. This is more granular than ZLE's line-level comparison.

---

## 3. Replxx (replxx_impl.cxx, terminal.cxx)

### State Tracking

Replxx uses simpler tracking:
- `_display`: UnicodeString accumulating rendered output
- `_displayInputLength`: Separates input from hints
- `_oldPos`: Previous cursor position
- `_modifiedState`: Flag indicating content changed

### Escape Sequences

```cpp
// Cursor movement
"\033[%dA"      // Move up n rows
"\033[%dB"      // Move down n rows
"\033[%dG"      // Absolute column (1-based)

// Clearing
"\033[J"        // Clear to end of screen
"\033c\033[H\033[2J\033[0m"  // Full reset + clear

// Bracketed paste
"\033[?2004h"   // Enable
"\033[?2004l"   // Disable
```

### Core Algorithm: `refresh_line()`

```cpp
void refresh_line(HINT_ACTION hintAction) {
    // 1. Rate limiting - skip if too soon
    if (duration < RAPID_REFRESH_US) {
        _refreshSkipped = true;
        return;
    }
    
    // 2. Render data with syntax highlighting
    render(_display);
    
    // 3. Handle hints
    handle_hints(hintAction);
    
    // 4. Calculate cursor position via virtual_render()
    //    - Simulates rendering to find cursor (row, col)
    
    // 5. Clear screen area
    //    - Move to line start
    //    - Clear to end of screen
    
    // 6. Write display buffer
    
    // 7. Reposition cursor
    jump_cursor(xPos, yOffset);
}
```

### Multiline Handling

```cpp
// Detect newlines
bool _hasNewlines;

// Navigation helpers
prev_newline_position();
next_newline_position();
pos_in_line();  // Cursor position within current line

// Indentation for continuation
indent();  // Adds spacing for subsequent lines
```

---

## 4. Common Patterns Across All Three

### 4.1 Virtual Screen Abstraction

All three maintain a complete virtual representation:
- Never query terminal for cursor position
- Internal state is authoritative
- Calculate positions during rendering, not during output

### 4.2 Prompt-Once Architecture

```
First render:
  1. Write prompt to terminal
  2. Write command
  3. Save state as "current"

Subsequent renders:
  1. Position to command start (absolute column)
  2. Clear command area only
  3. Write updated command
  4. Position cursor
  5. Update "current" state
```

Key: Never move to column 0 after first render--this protects the prompt from `\033[J`.

### 4.3 Relative vs Absolute Positioning

| Movement | Sequence | When to Use |
|----------|----------|-------------|
| Vertical relative | `\033[nA/B` | Moving within display |
| Horizontal absolute | `\033[nG` | Positioning on a line |
| Full absolute | `\033[row;colH` | Rarely--requires knowing physical row |

**Critical**: All use **relative vertical** + **absolute horizontal**. None use full absolute positioning because physical row numbers are unknown.

### 4.4 Width Calculation

All calculate visual width by:
1. Stripping ANSI escape sequences (zero width)
2. Decoding UTF-8 codepoints
3. Looking up character width (0, 1, or 2 columns)
4. Tracking position character-by-character

### 4.5 Dirty Tracking

| Editor | Granularity | Mechanism |
|--------|-------------|-----------|
| ZLE | Per-line | Compare old/new buffer elements |
| Fish | Per-character | `line_shared_prefix()` finds divergence point |
| Replxx | Per-refresh | `_modifiedState` flag, full line rewrite |

---

## 5. Lush LLE Current Architecture

### What LLE Has

```c
// screen_buffer.h - Virtual screen representation
typedef struct {
    screen_line_t lines[SCREEN_BUFFER_MAX_ROWS];
    int cursor_row, cursor_col;
    int command_start_row, command_start_col;
    // ...
} screen_buffer_t;
```

LLE already has:
- Virtual screen buffer with cell-level tracking
- Character-by-character rendering with width calculation
- Prompt-once architecture in display_controller.c
- Continuation prompt support via callbacks
- ANSI escape sequence handling

### What Was Removed (Session 92)

The broken differential code that was removed:
- `dirty_tracker.c` - Tracked changed regions but was never integrated
- `screen_buffer_diff()` - Compared buffers but used wrong coordinate system
- `screen_buffer_apply_diff()` - Tried to apply diff but used virtual rows as physical

### Current Approach

LLE uses "clear and redraw command area":
```c
// display_controller.c lines 482-550
1. Position to command_start_col with \033[nG
2. Handle ghost text cleanup (move down, clear, move back)
3. Clear from cursor to end of screen with \033[J
4. Write command text character-by-character
5. Position cursor with relative movement
```

This works but isn't differential--it redraws the entire command area every time.

---

## 6. Gap Analysis: LLE vs Modern Editors

### What LLE Does Right

| Feature | Status |
|---------|--------|
| Virtual screen buffer | OK Has it |
| Cell-level UTF-8 tracking | OK Has it |
| Prompt-once architecture | OK Implemented |
| Relative cursor movement | OK Uses `\033[nA/B` |
| Absolute column positioning | OK Uses `\033[nG` |
| Character-by-character width | OK Has it |

### What LLE Is Missing for True Differential

| Feature | Gap |
|---------|-----|
| Dual buffer comparison | Has `desired_screen` but no persistent `actual_screen` across renders |
| Line-level dirty tracking | Removed; would need to reimplement correctly |
| Shared prefix detection | Fish's `line_shared_prefix()` pattern not implemented |
| Selective line output | Currently clears all and rewrites all |

### Why Current Approach Works

The "clear and redraw" approach is:
- Simple and reliable
- Works on all terminals
- Sufficient for typical line editing performance
- Avoids complex coordinate translation bugs

The removed code failed because it tried to use virtual screen row numbers as physical terminal row numbers--a fundamental architectural error.

---

## 7. If Differential Updates Were Needed

### Correct Architecture

```
Render cycle:
1. Build desired_screen from prompt + command + menu
2. Compare desired_screen vs actual_screen line-by-line
3. For each changed line:
   a. Calculate relative movement from current position
   b. Move to that line (relative up/down)
   c. Position to divergence column (absolute)
   d. Clear to end of line
   e. Output changed portion
   f. Update current position tracking
4. Handle line count changes (clear extra or add new)
5. Position cursor
6. Copy desired_screen to actual_screen
```

### Key Implementation Points

1. **Track "current physical position"** during output, not "physical row number"
2. **Use relative vertical movement** (`\033[nA/B`) exclusively
3. **Use absolute horizontal** (`\033[nG`) for column positioning
4. **Never assume physical row numbers**--only relative deltas

### Data Structure Needs

```c
typedef struct {
    screen_buffer_t desired;     // Target state
    screen_buffer_t actual;      // Last rendered state
    int output_row;              // Current output position (relative)
    int output_col;              // Current output column
} differential_render_state_t;
```

---

## 8. Recommendation for Lush

### Short Term

The current "clear and redraw command area" approach is **sufficient**:
- Performance is acceptable for typical use
- Simpler code with fewer bugs
- Works reliably on all terminals

### Long Term (If Needed)

If differential updates become necessary (e.g., for very long lines or slow terminals):

1. **Keep `actual_screen` persistent** across render cycles
2. **Implement `line_changed()` comparison** (simpler than Fish's character-level)
3. **Track output position** as relative row + absolute column
4. **Output only changed lines** using relative movement

### What NOT To Do

- Don't try to convert virtual row numbers to physical row numbers
- Don't use `\033[row;colH` absolute positioning (physical row unknown)
- Don't query terminal for cursor position (unreliable, slow)

---

## References

### Source Code Analyzed

- **ZSH**: `Src/Zle/zle_refresh.c`, `Src/Zle/zle_main.c`
- **Fish**: `src/screen.rs`, `src/termsize.rs`
- **Replxx**: `src/replxx_impl.cxx`, `src/terminal.cxx`

### Lush Files

- `src/display/display_controller.c` - Current render implementation
- `src/display/screen_buffer.c` - Virtual screen buffer
- `include/display/screen_buffer.h` - Screen buffer types
