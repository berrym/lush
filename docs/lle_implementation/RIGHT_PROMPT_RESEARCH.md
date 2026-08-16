# Research: Right Prompt (RPROMPT/RPS1) Rendering Mechanisms

## Overview

This document details how ZSH/ZLE, Fish, and Replxx handle right-side prompts (RPROMPT/RPS1). The goal is to understand the exact rendering mechanism, escape sequences, and behavioral rules--then determine how to implement this in Lush LLE.

**Created**: 2026-01-01 (Session 95)
**Purpose**: Research reference for right prompt implementation
**Status**: Complete research, implementation guidance provided

---

## Executive Summary

Right prompts display information on the right edge of the terminal line. Key findings:

1. **ZSH and Fish support right prompts**; Replxx does not
2. **Positioning**: Move cursor to `terminal_width - rprompt_width - indent` using `\033[nG`
3. **Visibility rule**: Hide right prompt when `left_content_width + rprompt_width >= terminal_width`
4. **First line only**: Right prompt appears only on the primary prompt line, not continuation lines
5. **Dynamic hiding**: Right prompt disappears as user types and command line grows
6. **Save/restore cursor**: Use `\0337` (save) and `\0338` (restore) to return to command position

---

## 1. ZSH ZLE Right Prompt (zle_refresh.c)

### Configuration Variables

| Variable | Purpose |
|----------|---------|
| `RPS1` / `RPROMPT` | Right-side primary prompt (equivalent) |
| `RPS2` | Right-side continuation prompt |
| `ZLE_RPROMPT_INDENT` | Spacing from right edge (default: 1) |

### State Tracking

```c
/* Key variables in zle_refresh.c */
char *rpromptbuf;     // Rendered right prompt text
int rpromptw;         // Visual width of right prompt
int rprompth;         // Height (lines) - must be 1 for display
int put_rpmpt;        // Flag: should we display right prompt?
int oput_rpmpt;       // Previous frame's display state
int rprompt_off;      // Offset from right edge (ZLE_RPROMPT_INDENT)
```

### Visibility Decision

```c
/* From zle_refresh.c - right prompt visibility logic */
put_rpmpt = rprompth == 1 && rpromptbuf[0] && !strchr(rpromptbuf, '\t');

if (put_rpmpt) {
    rprompt_off = rprompt_indent;  // Usually 1
    put_rpmpt = (int)ZR_strlen(nbuf[0]) + rpromptw < winw - rprompt_off;
}
```

**Rules**:
1. Right prompt must be single-line (`rprompth == 1`)
2. Right prompt must not be empty
3. Right prompt must not contain tabs
4. Left content + right prompt must fit within terminal width

### Rendering Algorithm

```c
/* Simplified from zle_refresh.c */

// 1. Check if right prompt should be displayed
if (put_rpmpt && !iln && !oput_rpmpt) {
    // iln = current line number (0 = first line)
    // Only render on first line
    
    // 2. Move cursor to right prompt position
    int rprompt_col = winw - rprompt_off - rpromptw;
    moveto(0, rprompt_col);  // Row 0, calculated column
    
    // 3. Output right prompt
    zputs(rpromptbuf, shout);
    
    // 4. Return cursor to command position
    // (handled by subsequent moveto() calls)
}
```

### Cursor Movement

ZSH uses `moveto(row, col)` which internally uses:
- `TCHORIZPOS` (`\033[nG`) for absolute column positioning
- Direct cursor movement based on terminal capabilities

### Key Behaviors

1. **Disappears dynamically**: As command line grows, right prompt vanishes
2. **Reappears on delete**: When command shortens, right prompt returns
3. **First line only**: `!iln` check ensures only line 0 gets right prompt
4. **Tab prohibition**: Tabs in right prompt disable it entirely

---

## 2. Fish Shell Right Prompt (screen.rs)

### Positioning Logic

```rust
// From Fish screen.rs
fn render_right_prompt(&mut self, right_prompt: &str, screen_width: usize) {
    let right_prompt_width = fish_wcswidth(right_prompt);
    
    // Move cursor to right position
    self.r#move(screen_width - right_prompt_width, 0);
    
    // Write right prompt
    self.write_str(right_prompt);
}
```

### Visibility Decision

```rust
// Fish checks if right prompt fits
let fits = left_prompt_width + right_prompt_width <= screen_width;

if !fits {
    right_prompt_width = 0;  // Suppress right prompt
}
```

### Overlap Handling

Fish prevents overlap by:
1. Calculating total width needed: `left_prompt + command + autosuggestion + right_prompt`
2. If total exceeds `screen_width`, right prompt is hidden
3. Right prompt only renders on first/prompt line (`is_prompt_line` check)

### Multiline Behavior

```rust
// Right prompt only on first line
if is_prompt_line {
    // Render right prompt
} else {
    // Skip - continuation lines don't get right prompts
}
```

### Autosuggestion Interaction

Right prompt visibility accounts for autosuggestion ghost text:
- If command + autosuggestion would overlap right prompt, right prompt is hidden
- This prevents visual collision between ghost text and right prompt

---

## 3. Replxx

**Replxx does NOT support right prompts.**

The `Prompt` class only handles left-side prompts. There is no `RightPrompt` or equivalent functionality.

---

## 4. Terminal Escape Sequences for Right Prompts

### Essential Sequences

| Sequence | Name | Purpose |
|----------|------|---------|
| `\033[nG` | CHA | Cursor to absolute column n (1-based) |
| `\0337` | DECSC | Save cursor position |
| `\0338` | DECRC | Restore cursor position |
| `\033[K` | EL | Clear to end of line |
| `\033[s` | SCP | Save cursor (alternative) |
| `\033[u` | RCP | Restore cursor (alternative) |

### Right Prompt Rendering Sequence

```
1. Save cursor position:     \0337
2. Move to right position:   \033[{col}G  (where col = width - rprompt_width - indent + 1)
3. Write right prompt:       {rprompt_text}
4. Restore cursor position:  \0338
```

### Example for 80-column terminal with 10-char right prompt

```
Right prompt position = 80 - 10 - 1 = 69 (0-based), 70 (1-based for escape)

Output sequence:
  \0337           # Save cursor
  \033[70G        # Move to column 70
  [right text]    # Write right prompt
  \0338           # Restore cursor
```

---

## 5. Common Patterns and Rules

### 5.1 Visibility Rules (All Implementations)

| Rule | Description |
|------|-------------|
| **Width check** | `left_width + right_width < terminal_width` |
| **First line only** | Right prompt only on primary prompt line |
| **Single line** | Right prompt must not wrap |
| **No overlap** | Right prompt hidden if command would touch it |
| **Dynamic** | Visibility recalculated on each refresh |

### 5.2 Positioning Formula

```
right_prompt_column = terminal_width - rprompt_visual_width - indent

Where:
  terminal_width = current terminal columns
  rprompt_visual_width = visual width excluding ANSI codes
  indent = spacing from right edge (default 1 in ZSH, 0 in Fish)
```

### 5.3 Render Order

```
1. Calculate all widths (left prompt, command, right prompt)
2. Determine if right prompt fits
3. Render left prompt
4. If right prompt fits:
   a. Save cursor
   b. Move to right position
   c. Write right prompt
   d. Restore cursor
5. Render command
6. Position cursor in command
```

### 5.4 Edge Cases

| Case | Behavior |
|------|----------|
| Terminal resize | Recalculate and potentially hide/show right prompt |
| Multiline command | Right prompt on first line only |
| Wide characters | Properly calculate visual width |
| ANSI codes in rprompt | Strip for width calculation, preserve for output |
| Completion menu | Right prompt may be hidden during menu display |

---

## 6. Lush LLE Current State

### Configuration Support (COMPLETE)

```c
// theme.h
typedef struct lle_prompt_layout {
    char rps1_format[LLE_TEMPLATE_MAX];  // Right prompt template
    bool enable_right_prompt;             // Enable flag
    // ...
} lle_prompt_layout_t;
```

### Rendering Support (COMPLETE)

```c
// composer.c - Right prompt IS rendered
if (composer->config.enable_right_prompt && strlen(right_format) > 0) {
    lle_template_evaluate(right_format, &render_ctx, output->rprompt, ...);
    output->has_rprompt = true;
    output->rprompt_visual_width = calculate_visual_width(output->rprompt);
}
```

### Output Support (MISSING)

The rendered right prompt in `output->rprompt` is **never displayed**.

**Gap**: `display_controller.c` has no code to:
1. Check if right prompt fits
2. Save cursor position
3. Move to right edge
4. Output right prompt
5. Restore cursor

---

## 7. Implementation Approach for Lush

### Required Changes

#### 1. Display Controller Updates (`src/display/display_controller.c`)

```c
/* After writing left prompt, before writing command */

// Check if right prompt fits
if (output->has_rprompt) {
    int available = term_width - prompt_visual_width - 1;  // -1 for indent
    if (output->rprompt_visual_width < available) {
        // Save cursor
        write(STDOUT_FILENO, "\0337", 2);
        
        // Move to right position
        int col = term_width - output->rprompt_visual_width;
        char move_seq[16];
        snprintf(move_seq, sizeof(move_seq), "\033[%dG", col);
        write(STDOUT_FILENO, move_seq, strlen(move_seq));
        
        // Write right prompt
        write(STDOUT_FILENO, output->rprompt, output->rprompt_len);
        
        // Restore cursor
        write(STDOUT_FILENO, "\0338", 2);
    }
}
```

#### 2. Visibility Tracking

```c
/* Track right prompt state for differential updates */
typedef struct {
    bool rprompt_visible;        // Currently displayed?
    int rprompt_last_col;        // Column where it was placed
    size_t rprompt_last_width;   // Width when last displayed
} rprompt_state_t;
```

#### 3. Dynamic Hiding During Editing

```c
/* On each refresh, recalculate */
int left_content_width = prompt_width + command_width;
bool should_show = output->has_rprompt && 
                   (left_content_width + output->rprompt_visual_width + 1 < term_width);

if (was_visible && !should_show) {
    // Clear right prompt area
    // (handled by \033[K when we redraw the line)
}
```

### Rendering Flow

```
1. Compose prompt (composer already does this)
   - Renders PS1, PS2, RPS1 into output buffers
   - Calculates visual widths

2. First render (display_controller):
   a. Write left prompt
   b. Check if right prompt fits
   c. If yes: save cursor, move right, write rprompt, restore cursor
   d. Write command
   e. Position cursor

3. Subsequent renders:
   a. Check if right prompt visibility changed
   b. If now hidden: clear to end of line will remove it
   c. If now visible: render as in step 2
   d. Redraw command area
```

---

## 8. Configuration Options

### Suggested Options (following ZSH patterns)

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_right_prompt` | bool | false | Enable RPS1 |
| `rprompt_indent` | int | 1 | Spacing from right edge |
| `rprompt_on_continuation` | bool | false | Show on PS2 lines (RPS2) |

### Theme Configuration

```toml
[layout]
rps1 = "${time}"
enable_right_prompt = true
rprompt_indent = 1
```

---

## 9. Behavioral Summary

### When Right Prompt is Visible

- First line of prompt only
- When `left_prompt + command + rprompt + indent <= terminal_width`
- When right prompt is non-empty
- When right prompt is single-line

### When Right Prompt is Hidden

- On continuation lines (PS2)
- When command line grows too long
- When terminal is too narrow
- When right prompt is empty or disabled
- During completion menu display (optional)

### Dynamic Behavior

- Recalculated on every display refresh
- Disappears as user types (command grows)
- Reappears as user deletes (command shrinks)
- Recalculated on terminal resize (SIGWINCH)

---

## 10. References

### Source Code Analyzed

- **ZSH**: `Src/Zle/zle_refresh.c` - Right prompt rendering
- **Fish**: `src/screen.rs` - Screen update with right prompt
- **XTerm**: Control sequences documentation

### Terminal Standards

- ECMA-48: Control functions for coded character sets
- VT100/VT220: DEC terminal escape sequences
- XTerm: Extended control sequences
