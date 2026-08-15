# Screen Buffer Component Specification

**Component**: Screen Buffer System
**Version**: 1.0.0
**Created**: 2025-11-04
**Status**: Implemented and Tested
**Location**: `src/display/screen_buffer.{c,h}`
**Purpose**: Virtual terminal screen representation for accurate
line-wrapped cursor calculation and rendering

**Reading note (2026-05-23):** the original "Foundation for
Optimization: differential updates" framing in §2 is **not the
current implementation**. Lush uses prompt-once + clear-and-redraw
(`display_controller` is the sole stdout writer for the REPL); the
`dirty` flag, `screen_buffer_diff`, and `screen_buffer_apply_diff`
hooks are scaffolding kept in place for future re-evaluation but
are not on any render path today. Treat any "differential update"
language in this document as design context, not as current
behavior. This document is the authoritative account of how
rendering actually works.

---

## Table of Contents

1. [Overview](#overview)
2. [Design Rationale](#design-rationale)
3. [Architecture](#architecture)
4. [API Reference](#api-reference)
5. [Implementation Details](#implementation-details)
6. [Usage Examples](#usage-examples)
7. [Best Practices](#best-practices)
8. [Future Enhancements](#future-enhancements)

---

## Overview

The Screen Buffer component provides a **virtual representation of the terminal screen** specifically designed for rendering line-wrapped command input with proper UTF-8 support, cursor positioning, and ANSI escape sequence handling.

### Key Features

- **UTF-8 Aware**: Properly handles multi-byte characters using LLE's UTF-8 functions
- **Wide Character Support**: Correctly calculates display width for CJK characters and emoji (2 columns)
- **ANSI Escape Sequence Handling**: Skips escape sequences when calculating positions and byte offsets
- **Line Wrap Calculation**: Automatically handles text wrapping at terminal width boundaries
- **Cursor Position Tracking**: Calculates exact cursor row/column accounting for all complexities
- **Prompt Integration**: Handles prompt text separately from command text
- **Readline Marker Support**: Properly handles `\001` and `\002` markers used by readline

### Design Goals

1. **Accuracy**: Calculate exact cursor position in all scenarios (wrapping, UTF-8, ANSI codes)
2. **Separation of Concerns**: Isolate rendering logic from display output logic
3. **Foundation for Optimization**: Provide data structures for future differential updates
4. **Simplicity**: Clean API that's easy to use correctly
5. **Terminal Agnostic**: Works with any terminal width and character encoding

---

## Design Rationale

### Why Screen Buffer Was Created

#### Problem 1: Line Wrapping Display Issues
Before the screen buffer, line wrapping caused visible flicker and multiple redraws:
```
User types: "echo this is a very long command that wraps"
Problem: Full prompt+command redrawn on every keystroke after wrap
Result: Visible flicker, poor user experience
```

#### Problem 2: Cursor Positioning Failures
Cursor positioning logic was trying to track positions during output:
```c
// OLD APPROACH (broken):
int chars_since_cursor = 0;
// ... count characters while outputting ...
// Move LEFT by chars_since_cursor
// Problem: LEFT movement doesn't cross line boundaries!
```

When cursor needed to be on a previous wrapped line, `ESC[ND` (move LEFT) would fail because it can't move UP to previous lines.

#### Problem 3: UTF-8 Character Width Issues
Original code treated all bytes equally:
```c
// WRONG: Increments by 1 byte, breaks on UTF-8
cursor_position++;
```

This broke with:
- Multi-byte UTF-8 characters (emoji: 🔥 is 4 bytes but 2 columns)
- Wide characters (CJK: 中 is 3 bytes but 2 columns)
- ANSI escape sequences (shouldn't count toward position)

### The Solution: Virtual Screen Representation

The screen buffer creates a **virtual representation** of how the terminal will look:

```
┌─────────────────────────────────┐
│ $ echo "this wraps to multiple" │  Row 0
│ lines"                          │  Row 1
│                                 │
│ Cursor: (row=1, col=7)         │
└─────────────────────────────────┘
```

**Key Insight**: Calculate the entire screen layout ONCE, then use that information for output and cursor positioning. Don't try to track position during output.

---

## Architecture

### Data Structures

#### screen_cell_t
Represents a single character cell on the screen:

```c
typedef struct {
    char ch;            // Character (first byte for UTF-8)
    bool is_prompt;     // True if part of prompt, false if command
} screen_cell_t;
```

**Design Notes**:
- Currently stores only first byte (adequate for current use)
- `is_prompt` flag allows differential handling of prompt vs command
- Future: Could expand to store full UTF-8 sequences

#### screen_line_t
Represents one line of the terminal screen:

```c
typedef struct {
    screen_cell_t cells[SCREEN_BUFFER_MAX_COLS];  // Cell array
    int length;                                    // Actual length used
    bool dirty;                                    // Needs redraw (future use)
} screen_line_t;
```

**Design Notes**:
- `SCREEN_BUFFER_MAX_COLS = 512`: Supports ultra-wide terminals
- `length`: Actual characters in line (may be less than COLS)
- `dirty`: Foundation for differential update optimization

#### screen_buffer_t
Complete virtual screen representation:

```c
typedef struct {
    screen_line_t lines[SCREEN_BUFFER_MAX_ROWS];  // Line array
    int num_rows;                                  // Total rows used
    int terminal_width;                            // Terminal width in columns
    int cursor_row;                                // Cursor row (0-based)
    int cursor_col;                                // Cursor column (0-based)
} screen_buffer_t;
```

**Design Notes**:
- `SCREEN_BUFFER_MAX_ROWS = 128`: Handles extremely long wrapped commands
- `cursor_row/col`: Pre-calculated cursor position (key feature!)
- All positions are 0-based

### Component Relationships

```
┌─────────────────────────────────────────────────────────┐
│                  Display Controller                      │
│  (Handles terminal output and ANSI escape sequences)    │
└────────────┬────────────────────────────┬───────────────┘
             │ calls                       │ uses
             │                             │ cursor_row/col
             ↓                             ↓
    ┌────────────────────┐       ┌────────────────────┐
    │  screen_buffer_    │       │  screen_buffer_t   │
    │  render()          │──────→│  (virtual screen)  │
    └────────────────────┘       └────────────────────┘
             ↑                             
             │ uses UTF-8 functions
             │
    ┌────────────────────┐
    │ LLE UTF-8 Module   │
    │ - utf8_decode_     │
    │   codepoint()      │
    │ - utf8_codepoint_  │
    │   width()          │
    └────────────────────┘
```

### Processing Flow

```
1. INPUT: prompt_text, command_text, cursor_byte_offset, terminal_width
           ↓
2. PARSE PROMPT: Calculate visual width, handle ANSI codes
           ↓
3. PARSE COMMAND: Decode UTF-8, calculate widths, track wrapping
           ↓
4. TRACK CURSOR: When byte_offset reached, save (row, col)
           ↓
5. OUTPUT: screen_buffer_t with cursor_row/col calculated
           ↓
6. DISPLAY: Use cursor_row/col for terminal positioning
```

---

## API Reference

### Initialization and Cleanup

#### screen_buffer_init()
```c
void screen_buffer_init(screen_buffer_t *buffer, int terminal_width);
```

**Purpose**: Initialize a screen buffer for a given terminal width.

**Parameters**:
- `buffer`: Pointer to screen buffer to initialize
- `terminal_width`: Terminal width in columns (e.g., 80, 120)

**Behavior**:
- Clears all cells to null
- Sets `num_rows = 0`
- Sets `cursor_row = 0`, `cursor_col = 0`
- Stores `terminal_width`

**Example**:
```c
screen_buffer_t buffer;
screen_buffer_init(&buffer, 80);  // 80-column terminal
```

#### screen_buffer_clear()
```c
void screen_buffer_clear(screen_buffer_t *buffer);
```

**Purpose**: Clear all content from screen buffer.

**Parameters**:
- `buffer`: Screen buffer to clear

**Behavior**:
- Resets all cells to null character
- Resets `num_rows`, `cursor_row`, `cursor_col` to 0
- Preserves `terminal_width`

**Example**:
```c
screen_buffer_clear(&buffer);  // Reset for next render
```

#### screen_buffer_copy()
```c
void screen_buffer_copy(screen_buffer_t *dest, const screen_buffer_t *src);
```

**Purpose**: Deep copy one screen buffer to another.

**Parameters**:
- `dest`: Destination buffer
- `src`: Source buffer

**Behavior**:
- Performs complete `memcpy()` of entire structure
- Used to save "current" state for comparison with "desired" state

**Example**:
```c
screen_buffer_t current, desired;
// ... render into desired ...
screen_buffer_copy(&current, &desired);  // Save state
```

### Rendering

#### screen_buffer_render()
```c
void screen_buffer_render(
    screen_buffer_t *buffer,
    const char *prompt_text,
    const char *command_text,
    size_t cursor_byte_offset
);
```

**Purpose**: Core rendering function - converts prompt and command text into virtual screen representation with cursor position.

**Parameters**:
- `buffer`: Screen buffer to render into (must be initialized)
- `prompt_text`: Prompt string (may contain ANSI codes, readline markers)
- `command_text`: Command string (may contain ANSI codes, UTF-8, wide chars)
- `cursor_byte_offset`: Byte offset in command_text where cursor should be

**Behavior**:
1. Renders prompt text to buffer (skipping ANSI codes and readline markers)
2. Renders command text using UTF-8 decoding
3. Tracks cursor position when `cursor_byte_offset` is reached
4. Handles line wrapping at `terminal_width` boundaries
5. Updates `buffer->num_rows`, `buffer->cursor_row`, `buffer->cursor_col`

**Important Notes**:
- `cursor_byte_offset` is in RAW TEXT bytes (excludes ANSI escape sequences)
- UTF-8 characters counted by their byte length, not codepoint count
- Wide characters (2 columns) automatically handled
- If cursor_byte_offset not found, cursor set to (0, 0)

**Example**:
```c
screen_buffer_t buffer;
screen_buffer_init(&buffer, 80);

const char *prompt = "\033[32m$\033[0m ";  // Green $
const char *command = "echo hello 世界";    // Contains wide chars
size_t cursor_pos = 11;  // After "echo hello "

screen_buffer_render(&buffer, prompt, command, cursor_pos);

// Now buffer contains:
// - buffer.num_rows: Number of rows used
// - buffer.cursor_row: Row where cursor should be
// - buffer.cursor_col: Column where cursor should be
```

### Utility Functions

#### screen_buffer_visual_width()
```c
size_t screen_buffer_visual_width(const char *text, size_t byte_length);
```

**Purpose**: Calculate visual width of text (columns on screen).

**Parameters**:
- `text`: Text string to measure
- `byte_length`: Length in bytes

**Returns**: Number of columns the text will occupy on screen

**Behavior**:
- Skips ANSI escape sequences (0 width)
- Skips readline markers `\001` and `\002` (0 width)
- Single-byte ASCII = 1 column
- Multi-byte UTF-8 = 1 column (basic implementation)
- Future: Could use `lle_utf8_codepoint_width()` for wide char support

**Example**:
```c
const char *text = "\033[31mHello\033[0m";  // ANSI colored "Hello"
size_t width = screen_buffer_visual_width(text, strlen(text));
// width = 5 (ANSI codes not counted)
```

---

## Implementation Details

### UTF-8 Character Handling

The screen buffer uses LLE's UTF-8 functions for proper character decoding:

```c
// Decode UTF-8 codepoint
uint32_t codepoint;
int char_bytes = lle_utf8_decode_codepoint(
    command_text + i, 
    text_len - i, 
    &codepoint
);

// Get visual width (1 or 2 columns)
int visual_width = lle_utf8_codepoint_width(codepoint);

// Example results:
// 'A'  → char_bytes=1, visual_width=1
// '中' → char_bytes=3, visual_width=2 (wide character)
// '🔥' → char_bytes=4, visual_width=2 (emoji)
```

**Key Points**:
- Always use `lle_utf8_decode_codepoint()` - don't iterate byte-by-byte
- Always check `visual_width` - wide characters need 2 columns
- Track `bytes_processed` separately from visual position

### ANSI Escape Sequence Handling

ANSI sequences must be skipped when tracking byte offsets:

```c
// Detect escape sequence start
if (ch == '\033' || ch == '\x1b') {
    i++;  // Skip ESC
    if (i < text_len && command_text[i] == '[') {
        i++;  // Skip [
        // Skip until terminator
        while (i < text_len) {
            char c = command_text[i++];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
                c == 'm' || c == 'H' || c == 'J' || c == 'K' || c == 'G') {
                break;  // Found terminator
            }
        }
    }
    continue;  // DON'T increment bytes_processed
}
```

**Important**: ANSI sequences are NOT counted in `bytes_processed` because the cursor_byte_offset refers to RAW TEXT position, not including escape codes.

### Line Wrapping Logic

Line wrapping occurs when column reaches terminal width:

```c
static void write_char_to_buffer(
    screen_buffer_t *buffer,
    char ch,
    bool is_prompt,
    int *row,
    int *col
) {
    // Check if we need to wrap to next line
    if (*col >= buffer->terminal_width) {
        (*row)++;      // Move to next row
        *col = 0;      // Reset to column 0
        
        // Increase num_rows if needed
        if (*row >= buffer->num_rows) {
            buffer->num_rows = *row + 1;
        }
    }
    
    // Write character to current position
    if (*row < SCREEN_BUFFER_MAX_ROWS && 
        *col < SCREEN_BUFFER_MAX_COLS) {
        buffer->lines[*row].cells[*col].ch = ch;
        buffer->lines[*row].cells[*col].is_prompt = is_prompt;
        (*col)++;  // Move to next column
        
        // Update line length
        if (*col > buffer->lines[*row].length) {
            buffer->lines[*row].length = *col;
        }
    }
}
```

**Wide Character Wrapping**:
```c
// After writing first column of wide char
if (visual_width == 2) {
    col++;  // Advance by second column
    if (col >= buffer->terminal_width) {
        row++;  // Wrap to next line
        col = 0;
    }
}
```

### Cursor Position Tracking

Cursor position is tracked during rendering:

```c
bool cursor_set = false;
size_t bytes_processed = 0;  // Raw text bytes (no ANSI)

while (i < text_len) {
    // CHECK CURSOR POSITION BEFORE PROCESSING CHARACTER
    if (!cursor_set && bytes_processed == cursor_byte_offset) {
        buffer->cursor_row = row;
        buffer->cursor_col = col;
        cursor_set = true;
    }
    
    // ... process character ...
    bytes_processed += char_bytes;  // Count actual character bytes
}

// Handle cursor at end of text
if (!cursor_set && bytes_processed == cursor_byte_offset) {
    buffer->cursor_row = row;
    buffer->cursor_col = col;
    cursor_set = true;
}
```

**Critical**: Check cursor position BEFORE processing the character at that position, not after.

---

## Usage Examples

### Example 1: Basic Rendering

```c
#include "display/screen_buffer.h"

void render_prompt_and_command(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);
    
    const char *prompt = "$ ";
    const char *command = "echo hello";
    size_t cursor_pos = 10;  // At end
    
    screen_buffer_render(&buffer, prompt, command, cursor_pos);
    
    printf("Rendered %d rows\n", buffer.num_rows);
    printf("Cursor at (%d, %d)\n", buffer.cursor_row, buffer.cursor_col);
}
```

### Example 2: Handling Line Wrapping

```c
void render_wrapped_command(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 40);  // Narrow terminal
    
    const char *prompt = "$ ";
    const char *command = "echo this is a very long command that will wrap";
    size_t cursor_pos = 10;  // At "this"
    
    screen_buffer_render(&buffer, prompt, command, cursor_pos);
    
    // Check if wrapped
    if (buffer.num_rows > 1) {
        printf("Command wrapped to %d lines\n", buffer.num_rows);
    }
    
    // Cursor position accounts for wrapping
    printf("Cursor at row %d, col %d\n", 
           buffer.cursor_row, buffer.cursor_col);
}
```

### Example 3: UTF-8 and Wide Characters

```c
void render_unicode_command(void) {
    screen_buffer_t buffer;
    screen_buffer_init(&buffer, 80);
    
    const char *prompt = "$ ";
    const char *command = "echo 你好世界 🔥";  // Chinese + emoji
    size_t cursor_pos = 5;  // At space before Chinese
    
    screen_buffer_render(&buffer, prompt, command, cursor_pos);
    
    // Wide characters correctly handled
    // 你 (3 bytes, 2 columns)
    // 好 (3 bytes, 2 columns)
    // 世 (3 bytes, 2 columns)
    // 界 (3 bytes, 2 columns)
    // 🔥 (4 bytes, 2 columns)
    
    printf("Cursor at (%d, %d)\n", buffer.cursor_row, buffer.cursor_col);
}
```

### Example 4: Display Controller Integration

```c
void display_controller_redraw(void) {
    static screen_buffer_t current_screen;
    static screen_buffer_t desired_screen;
    static bool initialized = false;
    
    if (!initialized) {
        screen_buffer_init(&current_screen, 80);
        screen_buffer_init(&desired_screen, 80);
        initialized = true;
    }
    
    // Get prompt and command from layers
    char prompt[256];
    char command[4096];
    size_t cursor_pos;
    get_prompt_and_command(prompt, command, &cursor_pos);
    
    // Render into desired state
    screen_buffer_render(&desired_screen, prompt, command, cursor_pos);
    
    // Move cursor to start position
    if (current_screen.cursor_row > 0) {
        printf("\033[%dA", current_screen.cursor_row);  // Move UP
    }
    printf("\r");  // Column 0
    
    // Clear and redraw
    printf("\033[J");  // Clear to end of screen
    printf("%s", prompt);
    printf("%s", command);
    
    // Position cursor using calculated position
    int rows_up = desired_screen.num_rows - 1 - desired_screen.cursor_row;
    if (rows_up > 0) {
        printf("\033[%dA", rows_up);  // Move UP
    }
    printf("\r");  // Column 0
    if (desired_screen.cursor_col > 0) {
        printf("\033[%dC", desired_screen.cursor_col);  // Move RIGHT
    }
    
    // Save current state
    screen_buffer_copy(&current_screen, &desired_screen);
}
```

---

## Best Practices

### DO: Pre-calculate Then Use

✅ **CORRECT**:
```c
// Calculate layout first
screen_buffer_render(&buffer, prompt, command, cursor_pos);

// Use calculated values
move_cursor_to(buffer.cursor_row, buffer.cursor_col);
```

❌ **WRONG**:
```c
// Don't try to track during output
int pos = 0;
for (char *p = command; *p; p++) {
    putchar(*p);
    pos++;  // WRONG: Doesn't handle UTF-8, ANSI, wrapping!
}
```

### DO: Handle Edge Cases

✅ **CORRECT**:
```c
// Check cursor found
if (!cursor_set) {
    buffer->cursor_row = 0;
    buffer->cursor_col = 0;
}

// Check bounds
if (row >= SCREEN_BUFFER_MAX_ROWS) return;
if (col >= SCREEN_BUFFER_MAX_COLS) return;
```

### DO: Use Appropriate Buffer Sizes

```c
// For command input (typical)
screen_buffer_t buffer;  // 128 rows, 512 cols

// For very long commands
#define COMMAND_LAYER_MAX_HIGHLIGHTED_SIZE 16384
char command[COMMAND_LAYER_MAX_HIGHLIGHTED_SIZE];
```

### DON'T: Assume Single-Byte Characters

❌ **WRONG**:
```c
for (int i = 0; i < strlen(text); i++) {
    // Treats each byte as character - BREAKS on UTF-8
}
```

✅ **CORRECT**:
```c
size_t i = 0;
while (i < text_len) {
    uint32_t codepoint;
    int bytes = lle_utf8_decode_codepoint(text + i, text_len - i, &codepoint);
    // ... process codepoint ...
    i += bytes;
}
```

### DON'T: Count ANSI Sequences in Positions

❌ **WRONG**:
```c
bytes_processed++;  // For every byte including ANSI
```

✅ **CORRECT**:
```c
if (is_ansi_sequence) {
    // Skip without incrementing bytes_processed
    continue;
}
bytes_processed += char_bytes;
```

---

## Future Enhancements

### Differential Updates

The `dirty` flag in `screen_line_t` is reserved for future optimization:

```c
// Future: Only redraw changed lines
for (int row = 0; row < buffer->num_rows; row++) {
    if (desired.lines[row].dirty) {
        // Only redraw this line
        redraw_line(row, &desired.lines[row]);
    }
}
```

**Benefits**:
- Reduce terminal output
- Minimize cursor movement
- Improve performance for large wrapped commands

### Enhanced Cell Storage

Currently stores only first byte of UTF-8 sequences:

```c
// Future: Store complete UTF-8 sequences
typedef struct {
    char bytes[4];      // Full UTF-8 character (up to 4 bytes)
    uint8_t length;     // Actual bytes used
    uint8_t width;      // Display width (1 or 2 columns)
    bool is_prompt;
    // ... ANSI attributes ...
} screen_cell_t;
```

**Benefits**:
- Complete character representation
- Enable cell-by-cell differential updates
- Support for character attribute changes

### ANSI Attribute Tracking

```c
typedef struct {
    // ... existing fields ...
    uint8_t fg_color;
    uint8_t bg_color;
    uint8_t attributes;  // bold, underline, etc.
} screen_cell_t;
```

**Benefits**:
- Track color/style changes
- Enable smarter differential updates
- Reduce redundant ANSI sequence output

### Performance Monitoring

```c
typedef struct {
    uint64_t render_count;
    uint64_t total_render_time_ns;
    uint64_t max_render_time_ns;
    size_t max_rows_rendered;
} screen_buffer_stats_t;
```

**Benefits**:
- Identify performance bottlenecks
- Monitor real-world usage patterns
- Optimize hot paths

---

## Conclusion

The Screen Buffer component provides a robust foundation for terminal display rendering with proper support for:

- ✅ UTF-8 and wide characters
- ✅ ANSI escape sequences
- ✅ Line wrapping
- ✅ Accurate cursor positioning
- ✅ Future optimization opportunities

**Key Principle**: Calculate the complete screen layout once, then use that information for both rendering and cursor positioning. This separation of concerns creates a clean, maintainable architecture that works correctly in all scenarios.

---

## References

- **Implementation**: `src/display/screen_buffer.c`
- **Header**: `include/display/screen_buffer.h`
- **Usage**: `src/display/display_controller.c`
- **LLE UTF-8**: `src/lle/utf8_support.c`

---

**Document Version**: 1.0.0  
**Last Updated**: 2025-11-04
