# Research: Prompt-Once + Differential Updates Architecture

**Date:** 2025-11-20  
**Author:** Comprehensive Research Analysis  
**Purpose:** Document how modern line editors (Replxx, Fish, ZLE) implement differential screen updates while preserving prompts

---

## Executive Summary

After analyzing the source code of Replxx, Fish shell, and ZLE, I've discovered the critical architectural pattern that allows **prompt-once rendering** to coexist with **differential screen updates**:

### The Key Insight

**All three editors maintain VIRTUAL screen state that INCLUDES the prompt, but only output CHANGES to the physical terminal.**

The solution to our cursor positioning problem is:

1. **First render**: Write prompt + command manually to physical terminal
2. **Initialize screen buffers**: Set both `current` and `desired` to match what's on terminal (INCLUDING prompt position)
3. **Subsequent renders**: Update only `desired`, diff against `current`, output only changes
4. **Cursor positions**: Always relative to the VIRTUAL screen origin (top-left), not terminal origin

---

## 1. Replxx Architecture

### Key Data Structures

```cpp
class ReplxxImpl {
    Prompt _prompt;                    // Prompt state (text, width, cursor offset)
    UnicodeString _data;               // Input buffer
    UnicodeString _display;            // Rendered display (with colors/hints)
    Terminal _terminal;                // Terminal control
    int _displayInputLength;           // Where input ends vs hints
    
    // Cursor tracking
    int _pos;                          // Cursor position in _data
};
```

### First Render: `repaint()`

```cpp
void Replxx::ReplxxImpl::repaint( void ) {
    // Write prompt ONCE
    _prompt.write();
    
    // Add vertical space for multiline prompts
    for ( int i( _prompt._extraLines ); i < _prompt._cursorRowOffset; ++ i ) {
        _terminal.write8( "\n", 1 );
    }
    
    // Initial content render
    refresh_line( HINT_ACTION::SKIP );
}
```

**Critical Detail**: `_prompt._cursorRowOffset` tracks how many rows the prompt consumed. This becomes the vertical origin for all subsequent positioning.

### Subsequent Renders: `refresh_line()`

```cpp
void Replxx::ReplxxImpl::refresh_line( HINT_ACTION hintAction_ ) {
    render( hintAction_ );  // Rebuild _display buffer
    
    // Calculate cursor position RELATIVE TO PROMPT
    int xCursorPos( _prompt.indentation() );
    int yCursorPos( 0 );
    virtual_render( _data.get(), _pos, xCursorPos, yCursorPos );
    
    // Position at command start (RELATIVE MOVEMENT)
    _terminal.jump_cursor( 
        _prompt.indentation(),
        -( _prompt._cursorRowOffset - _prompt._extraLines )
    );
    
    // Write command text
    _terminal.write32( _display.data(), static_cast<int>( _display.size() ) );
    
    // Position cursor (RELATIVE MOVEMENT)
    _terminal.jump_cursor( xCursorPos, -( yEndOfInput - yCursorPos ) );
}
```

### Virtual Rendering: Cursor Calculation

```cpp
void virtual_render(
    char32_t const* text,
    int len,
    int& x,
    int& y,
    int width,
    int cursorPos
) {
    // x starts at prompt indentation
    for ( int i = 0; i < len; ++i ) {
        if ( text[i] == '\n' ) {
            ++y;
            x = promptLen_;  // Reset to prompt indent, NOT 0!
        } else {
            int w = mk_wcwidth( text[i] );
            x += w;
            if ( x >= width ) {
                x = 0;
                ++y;
            }
        }
    }
}
```

**Key Point**: After newline, `x` returns to `promptLen_` (prompt width), not 0. This means all cursor positions are relative to a VIRTUAL screen where prompt occupies the left margin.

### Terminal Control: `jump_cursor()`

```cpp
void Terminal::jump_cursor( int xOffset_, int yOffset_ ) {
    // Vertical movement (RELATIVE)
    if ( yOffset_ > 0 ) {
        snprintf( seq, sizeof(seq), "\033[%dB", yOffset_ );  // Down
    } else if ( yOffset_ < 0 ) {
        snprintf( seq, sizeof(seq), "\033[%dA", -yOffset_ ); // Up
    }
    
    // Horizontal movement (ABSOLUTE COLUMN)
    if ( xOffset_ != 0 ) {
        snprintf( seq, sizeof(seq), "\033[%dG", xOffset_ + 1 );
    }
}
```

**Critical**: Uses `\033[{n}G` for absolute column positioning, avoiding the `\r` problem.

---

## 2. Fish Shell Architecture

### Key Data Structures

```rust
pub struct Screen {
    actual: ScreenData,        // Current terminal state
    desired: ScreenData,       // Desired terminal state
    actual_left_prompt: Option<WString>,  // Last prompt written
    actual_width: Option<usize>,
}

pub struct ScreenData {
    line_datas: Vec<Line>,     // Screen lines
    cursor: Cursor,            // Cursor position
    screen_width: Option<usize>,
}
```

### First Render: Initialization

```rust
impl Default for Screen {
    fn default() -> Self {
        Self {
            actual: Default::default(),
            desired: Default::default(),
            actual_left_prompt: None,
            // ...
        }
    }
}
```

On first call to `update()`, `actual.screen_width` is `None`, triggering special initialization:

```rust
if self.actual.screen_width != screen_width {
    if self.actual.screen_width.is_some_and(|sw| sw > 0) {
        need_clear_screen = true;  // Only clear on resize, not first time
    }
    self.actual.screen_width = screen_width;
}
```

### Prompt Rendering and Caching

```rust
let prompt_changed = is_prompt_visible
    && (self.actual_left_prompt.as_ref()
        .is_none_or(|p| p != left_prompt)
    || self.actual.visible_prompt_lines 
       != self.desired.visible_prompt_lines);

if prompt_changed {
    self.move(0, 0);  // Move to screen origin
    
    // Render prompt
    self.actual_left_prompt = Some(left_prompt.to_owned());
    
    // Prompt is now in actual state
}
```

**Key Insight**: The prompt is stored in `actual_left_prompt` and only re-rendered when changed. The prompt content becomes part of the `actual` screen state.

### Differential Updates

```rust
fn line_shared_prefix(a: &Line, b: &Line) -> usize {
    let mut idx = 0;
    let max = a.text.len().min(b.text.len());
    
    while idx < max {
        if a.char_at(idx) != b.char_at(idx) 
            || a.color_at(idx) != b.color_at(idx) {
            break;
        }
        idx += 1;
    }
    idx
}

// In update():
let shared_prefix = line_shared_prefix(o_line(self, i), s_line(self, i));

// Only output characters after shared prefix
self.write_line(i, shared_prefix);
```

**Critical Pattern**: Compare `actual` (old) vs `desired` (new) line-by-line, only output differences. The prompt lines are compared just like any other line.

### Cursor Management

```rust
fn do_move(&mut self, new_x: usize, new_y: usize) {
    let y_steps = isize::try_from(new_y).unwrap() 
        - isize::try_from(self.actual.cursor.y).unwrap();
    
    // Vertical movement (RELATIVE)
    if y_steps < 0 {
        for _ in 0..(-y_steps) {
            self.outp.borrow_mut().write_command(CursorUp);
        }
    } else if y_steps > 0 {
        // ... down movement
    }
    
    // Horizontal movement
    if new_x < self.actual.cursor.x {
        // Move left
        for _ in 0..(self.actual.cursor.x - new_x) {
            self.outp.borrow_mut().write_command(CursorLeft);
        }
    }
    
    self.actual.cursor.x = new_x;
    self.actual.cursor.y = new_y;
}
```

**Key Point**: All cursor movements are RELATIVE to current position, tracked in `self.actual.cursor`.

---

## 3. ZLE (Zsh Line Editor) Architecture

### Key Data Structures

```c
// Video buffers
static REFRESH_STRING *nbuf;  // New buffer (desired state)
static REFRESH_STRING *obuf;  // Old buffer (actual state)

// Screen position tracking
static int vcs;               // Current cursor column
static int vln;               // Current cursor line
static int vmaxln;            // Max line number

// Prompt tracking
static int lpromptw;          // Left prompt width (columns)
static char *lpromptbuf;      // Left prompt text
```

### Initialization: `resetvideo()`

```c
void resetvideo(void) {
    // Allocate buffers
    nbuf = (REFRESH_STRING *)zshcalloc((winh + 1) * sizeof(*nbuf));
    obuf = (REFRESH_STRING *)zshcalloc((winh + 1) * sizeof(*obuf));
    
    // Reserve space for prompt at beginning of line 0
    if (lpromptw) {
        ZR_memset(nbuf[0], zr_sp, lpromptw);
        ZR_memset(obuf[0], zr_sp, lpromptw);
        nbuf[0][lpromptw] = obuf[0][lpromptw] = zr_zr;
    }
    
    // Set cursor to start after prompt
    vcs = lpromptw;
    vln = 0;
}
```

**Critical Detail**: Both `nbuf` and `obuf` reserve the first `lpromptw` columns for the prompt. This means the prompt is part of the virtual screen state, even though it's not physically redrawn.

### First Render: Prompt Output

```c
void zrefresh(void) {
    if (!clearflag && lpromptbuf[0]) {
        // Write prompt to terminal
        zputs(lpromptbuf, shout);
        
        if (lpromptwof == winw)
            zputs("\n", shout);  // Wrap if needed
            
        txtcurrentattrs = txtpendingattrs = pmpt_attr;
    }
    
    // Continue with buffer rendering...
}
```

### Subsequent Refreshes: Differential Updates

```c
void refreshline(int ln) {
    REFRESH_STRING nl = nbuf[ln];
    REFRESH_STRING ol = obuf[ln];
    
    // Find first difference
    for (; *nl && *ol && ZR_equal(*nl, *ol); nl++, ol++, ccs++) ;
    
    if (!*nl && !*ol) {
        // Lines are identical, skip
        return;
    }
    
    // Move to difference position
    if (ccs != vcs || ln != vln) {
        moveto(ln, ccs);
    }
    
    // Output changed portion
    for (; *nl; nl++) {
        putc_single_refresh(*nl);
    }
    
    // Clear any remaining old content
    if (*ol) {
        tcoutclear(TCCLEAREOL);
    }
}
```

**Key Pattern**: Compare `nbuf` (new) vs `obuf` (old) character-by-character, skip matching prefix, output only changes.

### Cursor Positioning: `moveto()`

```c
void moveto(int ln, int cl) {
    // Vertical movement
    if (ln < vln) {
        tc_upcurs(vln - ln);  // RELATIVE: move up
        vln = ln;
    }
    while (ln > vln) {
        if (vln < vmaxln - 1) {
            if (tc_downcurs(vmaxln - 1 - vln))  // RELATIVE: move down
                vcs = 0;
            vln = vmaxln - 1;
        } else {
            putc('\n');
            vln++;
            vcs = 0;
        }
    }
    
    // Horizontal movement
    if (cl != vcs) {
        singmoveto(cl);  // Uses optimal left/right/CR strategy
    }
}
```

**Key Point**: All movements are RELATIVE, tracked via `vcs` and `vln` state variables.

---

## 4. Common Patterns Across All Three

### Pattern 1: Virtual Screen State Includes Prompt

| Editor | Virtual State Structure |
|--------|-------------------------|
| **Replxx** | `_display` buffer starts at `_prompt.indentation()` column |
| **Fish** | `actual.line_datas` includes prompt lines |
| **ZLE** | `nbuf[0]` reserves first `lpromptw` columns for prompt |

**All three treat the prompt as part of the virtual screen coordinate system.**

### Pattern 2: Prompt Written Once, Preserved in State

| Editor | Mechanism |
|--------|-----------|
| **Replxx** | `_prompt._cursorRowOffset` tracks prompt position, never redrawn |
| **Fish** | `actual_left_prompt` caches prompt, only updates on change |
| **ZLE** | `clearflag` distinction: first render writes prompt, subsequent renders skip it |

**The prompt is physically written once, then becomes part of the comparison baseline.**

### Pattern 3: Differential Updates Compare Virtual States

| Editor | Comparison Strategy |
|--------|---------------------|
| **Replxx** | Rebuilds `_display`, outputs entire buffer (optimization: fast path for single-char append) |
| **Fish** | `line_shared_prefix()` compares line-by-line, outputs only differences |
| **ZLE** | `refreshline()` compares character-by-character, outputs changed portions |

**All maintain "old" and "new" virtual screen representations, outputting only changes.**

### Pattern 4: Relative Cursor Movement

| Editor | Movement Strategy |
|--------|-------------------|
| **Replxx** | `jump_cursor(x, y)` - vertical relative, horizontal absolute column |
| **Fish** | `do_move()` - both directions relative to `actual.cursor` |
| **ZLE** | `moveto()` - both directions relative to `vcs`/`vln` tracking |

**None use absolute terminal coordinates for row positioning.**

---

## 5. The Critical Mistake in Our Implementation

### What We Did Wrong

```c
// Our broken code (display_controller.c first render)
if (!prompt_rendered) {
    write(STDOUT_FILENO, prompt_buffer, strlen(prompt_buffer));
    // ... write command ...
    
    // WRONG: Using ABSOLUTE terminal coordinates
    char cursor_seq[32];
    snprintf(cursor_seq, sizeof(cursor_seq), "\033[%d;%dH", 
             desired_screen.cursor_row + 1,  // <- This is VIRTUAL row, not terminal row!
             desired_screen.cursor_col + 1);
    write(STDOUT_FILENO, cursor_seq, cursor_len);
}
```

**The Problem**: `desired_screen.cursor_row` is relative to where the virtual screen starts (row 0 = top of prompt), NOT relative to the physical terminal's top row.

If the prompt was drawn at terminal row 40, and `cursor_row = 2`, we're telling the terminal "go to row 2" (near top of terminal) when we should be at row 42.

### What We Should Do

Follow the pattern from all three editors:

```c
// CORRECT: Track cursor position relatively
static int current_virtual_row = 0;
static int current_virtual_col = 0;

if (!prompt_rendered) {
    // First render: write prompt + command
    write(STDOUT_FILENO, prompt_buffer, strlen(prompt_buffer));
    
    // Set current position to where prompt ends
    current_virtual_row = desired_screen.command_start_row;
    current_virtual_col = desired_screen.command_start_col;
    
    // Write command
    // ... (implementation)
    
    // Position cursor RELATIVELY
    int row_delta = desired_screen.cursor_row - current_virtual_row;
    int col_target = desired_screen.cursor_col;
    
    if (row_delta > 0) {
        printf("\033[%dB", row_delta);  // Down
    } else if (row_delta < 0) {
        printf("\033[%dA", -row_delta);  // Up
    }
    
    printf("\033[%dG", col_target + 1);  // Absolute column
    
    current_virtual_row = desired_screen.cursor_row;
    current_virtual_col = desired_screen.cursor_col;
    
    // Initialize screen buffers to match what's on terminal
    screen_buffer_copy(&current_screen, &desired_screen);
    
    prompt_rendered = true;
}
```

---

## 6. The Complete Solution Architecture

### Step 1: First Render (Initialization)

```c
static bool initialized = false;
static screen_buffer_t current_screen;
static screen_buffer_t desired_screen;

if (!initialized) {
    // 1. Write prompt to terminal
    write(STDOUT_FILENO, prompt_buffer, strlen(prompt_buffer));
    
    // 2. Build desired screen state (prompt + command + menu)
    screen_buffer_render_with_menu(&desired_screen, 
                                   prompt_buffer, 
                                   command_buffer,
                                   cursor_byte_offset,
                                   menu_text);
    
    // 3. Position cursor at command start (RELATIVE movement)
    printf("\033[%dG", desired_screen.command_start_col + 1);  // Absolute column
    
    // If prompt is multiline, we're already on the right row
    // Otherwise, we're on row 0 of virtual screen
    
    // 4. Clear from command start to end of screen
    write(STDOUT_FILENO, "\033[J", 3);
    
    // 5. Write command + menu
    // ... (handle continuation prompts, etc.)
    
    // 6. Position cursor (RELATIVE to current position)
    // Track where we are after writing, calculate relative movement
    
    // 7. CRITICAL: Initialize current_screen to match terminal state
    screen_buffer_copy(&current_screen, &desired_screen);
    
    initialized = true;
    return;
}
```

### Step 2: Subsequent Renders (Differential Updates)

```c
// Build new desired state
screen_buffer_render_with_menu(&desired_screen,
                               prompt_buffer,
                               command_buffer,
                               cursor_byte_offset,
                               menu_text);

// Generate diff between old (current_screen) and new (desired_screen)
screen_diff_t diff;
screen_buffer_diff(&current_screen, &desired_screen, &diff);

// Apply diff using RELATIVE movements
screen_buffer_apply_diff(&diff, STDOUT_FILENO);

// Update current_screen to match what's now on terminal
screen_buffer_copy(&current_screen, &desired_screen);
```

### Step 3: `screen_buffer_apply_diff()` Implementation

```c
void screen_buffer_apply_diff(const screen_diff_t *diff, int fd) {
    // Track current position in VIRTUAL screen coordinates
    static int current_row = 0;
    static int current_col = 0;
    
    for (int i = 0; i < diff->num_changes; i++) {
        const screen_change_t *change = &diff->changes[i];
        
        // Calculate RELATIVE movement needed
        int row_delta = change->row - current_row;
        int col_target = change->col;
        
        // Vertical movement (RELATIVE)
        if (row_delta > 0) {
            printf("\033[%dB", row_delta);  // Down
        } else if (row_delta < 0) {
            printf("\033[%dA", -row_delta);  // Up
        }
        
        // Horizontal movement (ABSOLUTE COLUMN)
        printf("\033[%dG", col_target + 1);
        
        // Apply change (write text, clear, etc.)
        // ...
        
        // Update tracked position
        current_row = change->row;
        current_col = change->col;
    }
}
```

**Key Point**: The `current_row`/`current_col` tracking is in VIRTUAL screen coordinates (0,0 = top-left of virtual screen), not terminal coordinates.

---

## 7. Why This Works

### The Virtual Screen Abstraction

```
Physical Terminal:           Virtual Screen (screen_buffer):
Row 0: [other content]       Row 0: [prompt line 1]
Row 1: [other content]       Row 1: [prompt line 2]
...                          Row 2: [command text...]
Row 38: [prompt line 1]      Row 3: [wrapped command...]
Row 39: [prompt line 2]      Row 4: [completion menu line 1]
Row 40: [command text...]    Row 5: [completion menu line 2]
Row 41: [wrapped command]    ...
Row 42: [menu line 1]
Row 43: [menu line 2]
```

The virtual screen ALWAYS starts at row 0, regardless of where the prompt was drawn on the physical terminal. Cursor movements are RELATIVE to the current virtual position, which the terminal translates to physical movements.

### The State Machine

```
State 1: First Render
  - Write prompt -> terminal cursor at (38, 0) physical
  - Build desired_screen -> virtual coordinates
  - Write command/menu -> using relative movements from (38, 0)
  - Initialize current_screen = desired_screen
  - Virtual (0,0) corresponds to physical (38,0)

State 2: Subsequent Renders
  - Build new desired_screen
  - Diff: current_screen vs desired_screen (both virtual)
  - Output changes using relative movements
  - Update: current_screen = desired_screen
  - Virtual coordinates remain consistent
```

**The mapping between virtual and physical is IMPLICIT** - we never need to know the absolute physical row. We only need to track where we are in virtual space and move relatively.

---

## 8. Implementation Checklist

### Phase 1: Fix First Render
- [ ] Remove absolute positioning `\033[{row};{col}H` 
- [ ] Add cursor position tracking (virtual coordinates)
- [ ] Use relative vertical movement + absolute column
- [ ] Initialize `current_screen` to match `desired_screen` after first render
- [ ] Handle continuation prompts correctly

### Phase 2: Fix Differential Updates
- [ ] Update `screen_buffer_apply_diff()` to track virtual position
- [ ] Change from absolute to relative vertical movement
- [ ] Keep absolute column positioning (safe and recommended)
- [ ] Ensure position tracking persists across calls

### Phase 3: Test Cases
- [ ] Cursor correct on first render
- [ ] Cursor correct after TAB (menu appears)
- [ ] Cursor correct after second TAB (menu cycles)
- [ ] Menu clears properly on backspace
- [ ] Multiline commands work correctly
- [ ] Wrapping at terminal edge works
- [ ] Works when prompt at bottom of terminal

---

## 9. Key Takeaways

### The Fundamental Principles

1. **Virtual Screen Abstraction**: The line editor maintains a virtual screen representation where (0,0) is the top-left of the prompt, regardless of physical terminal position.

2. **Prompt as Part of Virtual State**: The prompt occupies rows/columns in the virtual screen and is included in the differential comparison baseline.

3. **Relative Movement Only**: All cursor positioning uses relative movement from current position, never absolute terminal coordinates.

4. **State Initialization**: After first render, the "current" screen state must match what's actually on the terminal for differential updates to work.

5. **Position Tracking**: The system tracks its current position in virtual coordinates and calculates relative deltas for each movement.

### The Answer to Our Question

**Q**: How do you transition from prompt-once rendering to differential updates?

**A**: You don't "transition" - you use differential updates from the start. The "prompt-once" principle means:
- The prompt is rendered into the desired state only once (or when changed)
- The differential system sees "no change" in prompt area on subsequent updates
- Physical output of the prompt happens once; thereafter, diffs skip unchanged prompt lines

The key is that `current_screen` (old state) includes the prompt after first render, so subsequent diffs naturally skip it because it hasn't changed.

---

## 10. References

### Source Code

**Replxx**:
- `src/replxx_impl.cxx` - `repaint()`, `refresh_line()`
- `src/util.cxx` - `virtual_render()`
- `src/terminal.cxx` - `jump_cursor()`

**Fish**:
- `src/screen.rs` - `update()`, `do_move()`, `line_shared_prefix()`

**ZLE**:
- `Src/Zle/zle_refresh.c` - `zrefresh()`, `resetvideo()`, `refreshline()`, `moveto()`

### Key Concepts

- **Virtual Screen**: Internal representation with origin at (0,0) regardless of terminal position
- **Differential Updates**: Compare old vs new state, output only changes
- **Relative Movement**: Cursor positioning via delta from current position
- **Prompt Preservation**: Prompt in virtual state but only physically written when changed

---

**Document Version:** 1.0  
**Last Updated:** 2025-11-20  
**Status:** Complete - Ready for Implementation
