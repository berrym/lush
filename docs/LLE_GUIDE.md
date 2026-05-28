# LLE - Lush Line Editor

**The native line editor that makes Lush different**

---

## Table of Contents

1. [Why LLE Exists](#why-lle-exists)
2. [Editing Modes](#editing-modes)
3. [Emacs Mode Reference](#emacs-mode-reference)
4. [Vi Mode](#vi-mode)
5. [Completion System](#completion-system)
6. [Syntax Highlighting](#syntax-highlighting)
7. [History Integration](#history-integration)
8. [Multi-Line Editing](#multi-line-editing)
9. [Kill Ring](#kill-ring)
10. [Customization](#customization)
11. [Troubleshooting](#troubleshooting)

---

## Why LLE Exists

Every major shell wraps GNU Readline or implements something similar. Bash uses Readline. Zsh has ZLE. Fish has its own editor. They all follow the same pattern: a text input library adapted for shell use.

Lush took a different approach. LLE (Lush Line Editor) is a line editor designed from the ground up for interactive shell use. This isn't academic distinction - it enables capabilities that bolted-on solutions can't provide:

- **Deep completion integration**: LLE's completion system understands shell syntax at a fundamental level. It doesn't just complete filenames — it knows what arguments each shell builtin accepts and presents context-appropriate options. It also switches its first-word vocabulary to the debugger command set at the `(lush-debug)` break prompt.

- **Real-time syntax highlighting**: Every character you type is tokenized and colored. Commands are validated as you type. You see errors before pressing Enter.

- **Unified editing experience**: Multi-line editing, history navigation, and completion all work together without the seams you feel in other shells.

- **Foundation for the future**: Features like the integrated debugger, hook system, and shell modes all build on LLE's architecture.

LLE is not readline. It doesn't load `.inputrc`. It doesn't try to be compatible with readline's API. It's a purpose-built editor for a purpose-built shell.

---

## Editing Modes

LLE supports two editing paradigms:

### Emacs Mode (Default, Complete)

Emacs-style editing is the default and provides complete functionality. The full keybinding-action set is implemented and production-ready.

### Vi Mode (In Development)

Vi mode has the framework in place — mode switching, state machine, command parsing — but is not yet wired to editing operations. Track progress in [lle_specification/](lle_specification/) and the issue tracker. Use Emacs mode in the meantime.

To check the current mode:

```bash
display lle diagnostics
```

---

## Emacs Mode Reference

LLE implements the full set of GNU Readline-compatible keybinding actions.

### Cursor Movement

| Keybinding | Action | Description |
|------------|--------|-------------|
| `Ctrl-A` | Beginning of line | Move to start of current line |
| `Ctrl-E` | End of line | Move to end of current line |
| `Ctrl-F` | Forward char | Move forward one character |
| `Ctrl-B` | Backward char | Move backward one character |
| `Alt-F` | Forward word | Move forward one word |
| `Alt-B` | Backward word | Move backward one word |
| `Ctrl-P` / `Up` | Previous history | Previous history entry (or previous line in multi-line) |
| `Ctrl-N` / `Down` | Next history | Next history entry (or next line in multi-line) |
| `Alt-<` | Beginning of history | Go to first history entry |
| `Alt->` | End of history | Go to last history entry / current input |

### Text Deletion

| Keybinding | Action | Description |
|------------|--------|-------------|
| `Ctrl-D` | Delete char | Delete character at cursor (EOF on empty line) |
| `Backspace` | Backward delete | Delete character before cursor |
| `Ctrl-K` | Kill line | Kill from cursor to end of line |
| `Ctrl-U` | Backward kill line | Kill from beginning of line to cursor |
| `Alt-D` | Kill word | Kill from cursor to end of word |
| `Ctrl-W` | Backward kill word | Kill from beginning of word to cursor |
| `Alt-Backspace` | Backward kill word | Same as Ctrl-W |

### Kill Ring (Cut/Paste)

| Keybinding | Action | Description |
|------------|--------|-------------|
| `Ctrl-Y` | Yank | Paste most recent kill |
| `Alt-Y` | Yank pop | After Ctrl-Y, cycle through kill ring |

### Text Transformation

| Keybinding | Action | Description |
|------------|--------|-------------|
| `Ctrl-T` | Transpose chars | Swap character at cursor with previous |
| `Alt-T` | Transpose words | Swap word at cursor with previous word |
| `Alt-U` | Upcase word | Uppercase from cursor to end of word |
| `Alt-L` | Downcase word | Lowercase from cursor to end of word |
| `Alt-C` | Capitalize word | Capitalize first char, lowercase rest |

### Completion

| Keybinding | Action | Description |
|------------|--------|-------------|
| `Tab` | Complete | Trigger completion |
| `Alt-?` | Possible completions | List all possible completions |
| `Alt-*` | Insert completions | Insert all possible completions |

### History Search

| Keybinding | Action | Description |
|------------|--------|-------------|
| `Ctrl-R` | Reverse search | Incremental reverse history search |
| `Ctrl-S` | Forward search | Incremental forward history search |
| `Ctrl-G` | Abort | Cancel search, return to original line |

### Undo

| Keybinding | Action | Description |
|------------|--------|-------------|
| `Ctrl-_` | Undo | Undo last editing action |
| `Ctrl-X Ctrl-U` | Undo | Same as Ctrl-_ |

### Control

| Keybinding | Action | Description |
|------------|--------|-------------|
| `Ctrl-L` | Clear screen | Clear terminal, redraw prompt |
| `Ctrl-C` | Interrupt | Send SIGINT, cancel current input |
| `Ctrl-Z` | Suspend | Send SIGTSTP, suspend shell |
| `Ctrl-D` | EOF | On empty line: exit shell |
| `Enter` | Accept line | Execute the current line |

### Miscellaneous

| Keybinding | Action | Description |
|------------|--------|-------------|
| `Ctrl-Q` | Quoted insert | Insert next character literally |
| `Ctrl-V` | Quoted insert | Insert next character literally |
| `Alt-#` | Insert comment | Comment out line, add to history |
| `Ctrl-X Ctrl-E` | Edit and execute | Open in $EDITOR, execute on save |

---

## Vi Mode

Vi mode is in active development. The framework includes:

- Mode state machine (normal, insert, command-line)
- Motion commands parser
- Operator-pending mode
- Count prefixes

Target: Full vi mode in the v1.4.0 release.

When complete, vi mode will support:

**Normal Mode**
- `h j k l` - Movement
- `w b e` - Word movement
- `0 $ ^` - Line movement
- `x X` - Delete character
- `d` - Delete operator
- `c` - Change operator
- `y` - Yank operator
- `p P` - Paste
- `i a I A` - Enter insert mode
- `u Ctrl-R` - Undo/redo
- `/` `?` - Search
- `n N` - Search repeat

**Insert Mode**
- All text input
- `Escape` - Return to normal mode
- `Ctrl-[` - Return to normal mode

---

## Completion System

LLE's completion system is context-aware. It understands what you're typing and provides appropriate completions.

### Completion Types

| Type | Description | Example |
|------|-------------|---------|
| Command | Executables in $PATH | `gi<Tab>` -> `git` |
| Builtin | Shell builtins | `ec<Tab>` -> `echo` |
| File | Files and directories | `/etc/pa<Tab>` -> `/etc/passwd` |
| Variable | Shell variables | `$HO<Tab>` -> `$HOME` |
| Option | Command options | `ls --co<Tab>` -> `ls --color` |

### Context-Aware Builtin Completions

Every completable shell builtin has specific completion logic:

```bash
# set builtin knows its options
set -o <Tab>        # Shows: errexit nounset xtrace ...

# config builtin knows its structure
config set <Tab>    # Shows: shell completion display ...
config set shell.<Tab>  # Shows: errexit nounset pipefail ...

# debug builtin knows its subcommands
debug <Tab>         # Shows: on off vars print trace profile analyze ...

# display builtin knows its arguments (including LLE Phase 3 trio)
display <Tab>       # Shows: lle features themes status stats widget hook segment
display lle <Tab>   # Shows: diagnostics status info widget hook segment
```

### Debug-Prompt Completion

At the `(lush-debug)` break prompt, LLE's first-word completion
switches to the debugger command vocabulary (`step`, `next`, `out`,
`continue`, `print`, `vars`, `stack`, `type`, `t`, `watch`,
`break`, `quit`, ...). The shell-side first-word sources sit out
via a `lle_in_debug_prompt()` gate; variable completion (`$x`) and
file completion remain available for argument positions.

### Menu Navigation

When multiple completions are available, a menu appears:

| Key | Action |
|-----|--------|
| `Up` / `Ctrl-P` | Move up in menu |
| `Down` / `Ctrl-N` | Move down in menu |
| `Left` / `Ctrl-B` | Move left (multi-column) |
| `Right` / `Ctrl-F` | Move right (multi-column) |
| `Tab` | Accept selection |
| `Enter` | Accept and execute |
| `Escape` | Cancel completion |

### Completing External Commands

When an external command shadows a builtin (like `/usr/bin/echo` vs the `echo` builtin), selecting the external command inserts its full path so you explicitly use the external version.

---

## Syntax Highlighting

LLE tokenizes your input in real-time and applies colors based on token types.

### Token Types

LLE recognizes a broad token vocabulary covering every construct
the shell can parse:

**Commands**
- Valid external command (green)
- Invalid command (red)
- Shell builtin (cyan)
- Alias (cyan)
- Function (blue)

**Strings**
- Single-quoted string
- Double-quoted string
- ANSI-C quoted string (`$'...'`)
- Backtick command substitution

**Variables**
- Variable reference (`$var`, `${var}`)
- Special variables (`$?`, `$#`, `$@`, `$$`, `$!`)

**Paths**
- Valid file/directory path (underlined green)
- Invalid path (underlined red)

**Operators**
- Pipe (`|`)
- Redirections (`>`, `<`, `>>`, `2>`, `&>`)
- Background (`&`)
- Command separator (`;`)
- Logical AND/OR (`&&`, `||`)
- Subshell parentheses
- Brace groups

**Other**
- Keywords (`if`, `then`, `else`, `fi`, `for`, `while`, `do`, `done`, `case`, `esac`)
- Comments (`# ...`)
- Options (`-v`, `--verbose`)
- Glob patterns (`*`, `?`, `[...]`)
- Here-documents
- Process substitution
- Arithmetic expansion
- Errors

### Command Validation

LLE checks whether commands exist as you type:

- Commands that exist appear in green
- Commands that don't exist appear in red
- Builtins appear in cyan
- Aliases and functions have their own colors

This validation uses a cache for performance - commands are only checked once per session unless the cache is cleared.

### Theme Integration

Syntax highlighting colors come from the current theme:

```bash
# List available themes
theme list

# Switch themes
theme set dark

# Colors update immediately
```

---

## History Integration

LLE integrates with Lush's history system.

### Navigation

| Key | Action |
|-----|--------|
| `Up` / `Ctrl-P` | Previous history entry |
| `Down` / `Ctrl-N` | Next history entry |
| `Alt-<` | First history entry |
| `Alt->` | Last entry / current line |

### Incremental Search

| Key | Action |
|-----|--------|
| `Ctrl-R` | Start reverse search |
| `Ctrl-S` | Start forward search |
| Type text | Narrow search |
| `Ctrl-R` again | Find previous match |
| `Ctrl-S` again | Find next match |
| `Enter` | Accept match |
| `Ctrl-G` | Cancel, restore original |

### History Expansion

Standard history expansion works:

```bash
!!              # Last command
!$              # Last argument of last command
!^              # First argument of last command
!*              # All arguments of last command
!n              # Command number n
!-n             # n commands ago
!string         # Most recent command starting with string
!?string?       # Most recent command containing string
```

### Multi-Line History

Multi-line commands are stored as single history entries. Navigating to a multi-line command displays all lines.

### Deduplication

LLE can deduplicate adjacent identical history entries:

```bash
config set history.dedup_navigation true
```

With this enabled, pressing Up will skip repeated commands.

---

## Multi-Line Editing

LLE handles multi-line input naturally.

### Automatic Continuation

When a line is syntactically incomplete, LLE provides a continuation prompt:

```bash
$ for i in 1 2 3; do
>     echo "Number: $i"
> done
Number: 1
Number: 2
Number: 3
```

Incomplete constructs that trigger continuation:
- Unclosed quotes
- Unclosed parentheses/braces
- `if`/`then`/`else`/`fi` blocks
- `for`/`while`/`do`/`done` loops
- `case`/`esac` statements
- Backslash at end of line

### Editing Within Multi-Line

| Key | Action |
|-----|--------|
| `Up` | Move to previous line (not history) |
| `Down` | Move to next line (not history) |
| `Ctrl-A` | Start of current line |
| `Ctrl-E` | End of current line |
| `Alt-<` | Start of buffer |
| `Alt->` | End of buffer |

LLE maintains column position when moving between lines (sticky column).

### Explicit Newline

To insert a newline without executing:

- Backslash at end of line: `\<Enter>`
- Within quotes, just press Enter

---

## Kill Ring

LLE maintains a kill ring for cut/paste operations.

### Adding to Kill Ring

These operations add to the kill ring:
- `Ctrl-K` - Kill to end of line
- `Ctrl-U` - Kill to beginning of line
- `Alt-D` - Kill word forward
- `Ctrl-W` / `Alt-Backspace` - Kill word backward

### Yanking

| Key | Action |
|-----|--------|
| `Ctrl-Y` | Yank (paste) most recent kill |
| `Alt-Y` | After Ctrl-Y, cycle through older kills |

### Kill Ring Behavior

- Consecutive kill commands append to the same entry
- The ring holds multiple entries (typically 10)
- `Alt-Y` only works immediately after `Ctrl-Y`

---

## Customization

LLE exposes three first-class customization surfaces — widgets,
hooks, and segments — all routed through the central configuration
registry. They are the LLE Phase 3 customization trio.

### Widgets: User-Defined Editing Actions

A widget is a named shell function bound to a key sequence so it
behaves as a one-shot editing operation. Equivalent to `zle -N` in
zsh, but available in every mode.

```bash
# Define a widget body
my-uppercase-word() {
    # Widget callbacks operate on the active LLE buffer
    lle buffer transform "${WIDGET_WORD}" upper
}

# Register and bind
display lle widget add  my-uppercase-word my-uppercase-word
display lle widget bind '\eU' my-uppercase-word

# Inspect
display lle widget list
display lle widget show my-uppercase-word
display lle widget remove my-uppercase-word
```

User widgets sit alongside builtin widgets in the same dispatcher;
they can be bound from `~/.config/lush/lushrc` or a `keybindings.toml`
exactly like built-ins.

### Hooks: Lifecycle Callbacks

`precmd`, `preexec`, `chpwd`, and `periodic` shell functions are the
canonical hook surface (see [HOOKS_AND_PLUGINS.md](HOOKS_AND_PLUGINS.md)).
For symmetric configuration alongside widgets and segments, the same
hooks can be registered with `display lle hook`:

```bash
display lle hook add  precmd  my_precmd_widget
display lle hook list
display lle hook remove precmd my_precmd_widget
```

The full hook-type catalog covers ten LLE-internal events; the
shell-event hub bridges to PRE_COMMAND / POST_COMMAND at editor
init.

### Segments: Custom Prompt Pieces

A segment is a prompt fragment whose visibility and contents are
driven by a shell variable. Useful for context indicators (active
kubernetes context, current AWS profile, virtualenv name) that
update on every prompt redraw without re-parsing PS1.

```bash
# A precmd hook keeps the variable current
precmd() {
    KUBE_CTX="${KUBECONFIG:+k8s:$(basename "$KUBECONFIG")}"
}

# Register a segment backed by that variable
display lle segment add kube_ctx KUBE_CTX
display lle segment list
display lle segment show kube_ctx
display lle segment remove kube_ctx
```

The segment renders whenever `$KUBE_CTX` is non-empty; visibility
flips automatically without conditional logic in the prompt
template.

### Keybinding Configuration

LLE keybindings can be configured through the config system:

```bash
# View current bindings
config show keybindings

# Modify a binding (example syntax)
config set keybindings.ctrl_a beginning_of_line
```

### Completion Configuration

```bash
# Enable/disable completion
config set completion.enabled true

# Case sensitivity
config set completion.case_sensitive false

# Menu display
config set completion.menu_complete true
```

### Syntax Highlighting Configuration

```bash
# Enable/disable
config set display.syntax_highlighting true

# Command validation
config set display.validate_commands true

# Path validation  
config set display.validate_paths true
```

### History Configuration

```bash
# History size
config set history.size 10000

# Deduplication
config set history.dedup_navigation true

# Ignore patterns
config set history.ignore_patterns "^\\s*$"
```

---

## Troubleshooting

### Display Issues

**Problem**: Colors don't appear
```bash
# Check terminal color support
echo $TERM
tput colors

# Ensure color is enabled
config set display.syntax_highlighting true
```

**Problem**: Characters display incorrectly
```bash
# Check locale
echo $LANG $LC_ALL

# LLE requires UTF-8
export LANG=en_US.UTF-8
```

**Problem**: Width calculation wrong
```bash
# Force terminal size refresh
# Resize terminal window, or:
kill -WINCH $$
```

### Completion Issues

**Problem**: No completions appear
```bash
# Check completion is enabled
config get completion.enabled

# Check for errors
display lle diagnostics
```

**Problem**: Wrong completions
```bash
# Clear completion cache
# (Rebuilds command cache)
```

### History Issues

**Problem**: History not saving
```bash
# Check history file permissions
ls -la ~/.lush_history

# Check history size
config get history.size
```

### Performance Issues

**Problem**: Slow response
```bash
# Check display stats
display stats

# Disable expensive features temporarily
config set display.validate_paths false
config set display.validate_commands false
```

### Getting Diagnostics

```bash
# Full LLE status
display lle diagnostics

# Performance statistics
display stats

# Feature status
display features
```

---

## Technical Details

### Architecture

LLE is composed of several subsystems:

| Subsystem | Responsibility |
|-----------|----------------|
| Buffer | Text storage, cursor management |
| Input | Key parsing, sequence detection |
| Keybinding | Action dispatch |
| Completion | Context analysis, candidate generation |
| History | Storage, navigation, search |
| Display | Rendering, syntax highlighting |
| Terminal | ANSI output, capability detection |

### Unicode Support

LLE fully supports Unicode:

- UTF-8 input and output
- Grapheme cluster handling (emoji, combining characters)
- Correct cursor positioning for wide characters
- Unicode-aware word boundaries

### Memory Management

LLE uses:

- Arena allocators for temporary data
- Memory pools for frequent allocations
- Reference counting for shared data

Zero memory leaks on normal operation (verified with macOS leaks tool).

### Thread Safety

LLE is not thread-safe. All operations must occur on the main thread. The async worker (for completion) uses careful synchronization.

---

## See Also

- [USER_GUIDE.md](USER_GUIDE.md) - Complete shell reference
- [COMPLETION_SYSTEM.md](COMPLETION_SYSTEM.md) - Detailed completion documentation
- [CONFIGURATION.md](CONFIGURATION.md) - Configuration reference
