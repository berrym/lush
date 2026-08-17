# LLE Release Roadmap

**Created**: 2025-11-30 (Session 37)
**Last Updated**: 2025-11-30 (snapshot); reading note 2026-05-23
**Status**: Active Development

**Reading note (2026-05-23):** point-in-time snapshot from
2025-11-30. Since then most of the "Missing" priority items have
landed: Ctrl+R history search, undo/redo, `display lle`
subcommands, builtin completion context, and the LLE Phase 3
customization trio (`display lle widget`/`hook`/`segment`). The
GNU-Readline fallback option remains the compile-time escape
hatch; LLE has been the default for months. Current LLE feature
status is in the [README](../../README.md) status table and
[USER_GUIDE.md](../USER_GUIDE.md) "Line Editing (LLE)" section.
Treat this document as a historical roadmap; the README/USER_GUIDE
are the current truth.

---

## Philosophy

Lush development prioritizes **correctness over speed**. There is no external pressure 
for releases - the goal is to build a truly great shell, not to ship quickly. Features 
are implemented thoroughly, with proper architecture, testing, and documentation.

GNU Readline remains available as a battle-tested fallback (`LLE_ENABLED=0`) until LLE 
has proven itself through extended real-world use by the developer.

---

## Current State (Post-Session 37)

### What's Working

| Feature | Status | Notes |
|---------|--------|-------|
| `lle_readline()` orchestration | OK Complete | Full event pipeline functional |
| Emacs keybindings | OK Complete | Full GNU Readline compatibility |
| Completion system | OK Complete | Multi-column menu, fuzzy matching |
| Completion menu navigation | OK Complete | Arrow keys, vim-style (hjkl) |
| History system | OK Complete | Navigation, add-time & nav-time dedup |
| Autosuggestions | OK Complete | Fish-style, partial accept (Ctrl+Right) |
| Syntax highlighting | OK Complete | Themeable, 6 built-in themes |
| Widget system | OK Complete | 24 builtin widgets, lifecycle hooks |
| Display pipeline | OK Complete | Layered architecture, event-driven |
| UTF-8/Unicode support | OK Complete | Grapheme clusters, wide chars |
| Multiline editing | OK Complete | Dynamic continuation prompts |
| Ctrl+G abort | OK Complete | Tiered dismissal, ZSH-style recovery |

### What's Missing (Priority Order)

| Priority | Feature | Status | Effort | Blocker? |
|----------|---------|--------|--------|----------|
| **P1** | Ctrl+C signal handling | OK Complete | Done | NO - implemented |
| **P2** | macOS compatibility | FAIL Untested | 1 week | YES - cross-platform required |
| **P3** | Ctrl+R history search | FAIL Missing | 3-5 days | YES - fundamental feature |
| **P4** | Undo/Redo | FAIL Missing | 3-5 days | YES - editing safety |
| **P5** | `display lle` subcommands | FAIL Missing | 2-3 days | No - polish |
| **P6** | Builtin completion context | FAIL Missing | 3-5 days | No - polish |
| **P7** | Make readline optional | FAIL Not started | 2-3 days | No - technical debt |

### Deferred (Not Priority)

| Feature | Reason | When to Revisit |
|---------|--------|-----------------|
| Vi keybindings | Even vi users use Emacs in shells | After master merge, if requested |
| BSD support | Linux + macOS covers 99% of users | After macOS proven stable |
| Performance dashboard | Current perf is adequate | If profiling shows need |

---

## Priority 1: Ctrl+C Signal Handling OK COMPLETE

**Implemented**: 2025-11-30 (Session 38)

### Solution Architecture

The fix required coordination between lush's signal handler and LLE's input loop:

1. **Signal Handler (`src/signals.c`)**:
   - Added `lle_readline_active` flag to track when LLE readline is running
   - Added `sigint_received_during_readline` flag set by SIGINT handler
   - When SIGINT received during LLE readline, sets flag instead of printing newline
   - New APIs: `set_lle_readline_active()`, `check_and_clear_sigint_flag()`

2. **LLE Readline (`src/lle/lle_readline.c`)**:
   - Sets `lle_readline_active(1)` after entering raw mode
   - Main input loop checks `check_and_clear_sigint_flag()` at start of each iteration
   - On SIGINT: echoes `^C\n`, clears completion/autosuggestion, resets display, returns empty string
   - Sets `lle_readline_active(0)` before cleanup

### Files Modified

- `src/signals.c` - Added LLE coordination flags and functions
- `include/signals.h` - Declared new API functions
- `src/lle/lle_readline.c` - Added SIGINT check in input loop, set/clear active flag

### Behavior

- Ctrl+C on empty buffer: shows `^C` and new prompt OK
- Ctrl+C mid-input: clears line, shows `^C` and new prompt OK
- Ctrl+C with completion menu: dismisses menu, clears line OK
- Ctrl+C with autosuggestion: clears suggestion, aborts line OK
- No terminal corruption after Ctrl+C OK
- No zombie processes or resource leaks OK

---

## Priority 2: macOS Compatibility

### Why Important?

A shell that only works on Linux is not a serious shell. macOS is widely used by 
developers and must be a first-class platform.

### Known Potential Issues

- `termios` differences (macOS uses older API variants)
- `TIOCGWINSZ` / terminal size detection
- `clock_gettime()` availability (older macOS)
- Signal handling differences
- File system case sensitivity
- `/proc` filesystem absence

### Tasks

- [ ] Set up macOS build environment (or VM/CI)
- [ ] Compile lush on macOS, fix build errors
- [ ] Run LLE test suite on macOS
- [ ] Manual testing of all interactive features
- [ ] Fix any termios/terminal handling issues
- [ ] Verify signal handling works correctly
- [ ] Document any macOS-specific considerations

### Success Criteria

- Clean build on macOS (no warnings ideally)
- All existing tests pass
- Interactive LLE functions identically to Linux
- No macOS-specific crashes or display corruption

---

## Priority 3: Ctrl+R Interactive History Search

### Why Important?

`Ctrl+R` reverse search is one of the most-used shell features for power users. 
Typing `Ctrl+R` then a substring to find matching history is fundamental workflow.

### Specification

**Activation**: `Ctrl+R` enters incremental search mode

**Behavior**:
- Prompt changes to `(reverse-i-search)'query': matching_command`
- Each keystroke filters history in real-time
- `Ctrl+R` again: next older match
- `Ctrl+S`: next newer match (forward search)
- `Enter`: accept current match, execute
- `Ctrl+G` or `ESC`: cancel search, restore original line
- Arrow keys: accept match, exit search mode, allow editing
- Any other key: accept match, insert key

**Display**:
- Show search query and matching command
- Highlight matching portion in result
- Integrate with LLE display pipeline (not direct terminal writes)

### Implementation Approach

1. Add search mode state to `readline_context_t`
2. Create `lle_reverse_search_context()` action
3. Modify main loop to handle search mode input differently
4. Use existing history system for filtering
5. Render search state through display pipeline

### Success Criteria

- `Ctrl+R` activates search mode with visual feedback
- Incremental filtering works as user types
- Multiple `Ctrl+R` presses cycle through matches
- All exit methods work correctly (Enter, Ctrl+G, ESC, arrows)
- Integrates with autosuggestions (disable during search)
- No display corruption

---

## Priority 4: Undo/Redo

### Why Important?

Editing mistakes happen. Without undo, users must retype from scratch. This is 
especially frustrating with long commands or complex pipelines.

### Specification

**Keybindings**:
- `Ctrl+_` or `Ctrl+/`: Undo
- `Alt+_` or custom: Redo (GNU Readline doesn't have standard redo)

**Behavior**:
- Each editing operation creates an undo point
- Undo restores previous buffer state and cursor position
- Redo moves forward through undo history
- Typing after undo clears redo stack
- Undo stack persists for current line only (cleared on Enter)

### Implementation Approach

1. Create undo stack structure in `readline_context_t` or `lle_buffer_t`
2. Snapshot state before each modification
3. Implement `lle_undo()` and `lle_redo()` actions
4. Bind to standard keys
5. Consider memory limits (max undo depth)

### Success Criteria

- Undo reverses all editing operations (insert, delete, kill, yank, etc.)
- Redo works after undo
- Typing clears redo stack
- Memory usage bounded (configurable max depth)
- Cursor position restored correctly

---

## Priority 5: `display lle` Subcommands

### Why Important?

Users should be able to configure LLE at runtime without editing config files. 
The `display` builtin already has `display lle enable/disable` - expand this.

### Proposed Subcommands

```
display lle status          # Show current LLE state and settings
display lle enable          # Enable LLE (existing)
display lle disable         # Disable LLE (existing)
display lle theme <name>    # Change syntax highlighting theme
display lle autosuggestions on|off
display lle completion on|off  
display lle syntax on|off   # Syntax highlighting
display lle history-dedup on|off
display lle config          # Show all LLE config values
display lle reset           # Reset to defaults
```

### Implementation Approach

1. Expand `cmd_display_lle()` in display builtin
2. Add subcommand parsing
3. Connect to existing config system
4. Ensure changes take effect immediately (no restart required)

### Success Criteria

- All subcommands work and provide feedback
- Changes persist via config system if desired
- Tab completion for subcommands and options
- Help text for each subcommand

---

## Priority 6: Builtin Completion Context

### Why Important?

Professional shells provide context-aware completions. When user types `cd <TAB>`, 
they get directories. When they type `git <TAB>`, they get git subcommands.

### Scope

Focus on lush's own builtins first:
- `display` - complete subcommands (theme, lle, etc.)
- `config` - complete config keys and values  
- `history` - complete subcommands
- `alias`/`unalias` - complete alias names
- `cd` - directories only
- `source` - files only

### Implementation Approach

1. Define completion context structure per builtin
2. Register contexts during builtin initialization
3. Completion system queries context based on command prefix
4. Return appropriate candidates (subcommands, files, custom)

### Success Criteria

- Each builtin has appropriate completions
- Subcommand options complete correctly
- File/directory filtering works
- No regression in general completion

---

## Priority 7: Make Readline Optional

### Why Important?

GNU Readline is currently a required dependency even though LLE can function 
independently. Making it optional:
- Reduces binary size
- Removes external dependency for LLE-only builds
- Proves LLE is truly complete

### Approach

1. Audit all readline usage in codebase
2. Create abstraction layer where needed
3. Add meson option: `readline_support` (default: true for now)
4. Conditionally compile readline integration
5. Ensure `LLE_ENABLED=0` gracefully handles missing readline

### Success Criteria

- Build succeeds with `-Dreadline_support=false`
- LLE functions fully without readline
- Fallback mode (`LLE_ENABLED=0`) either works with basic input or errors gracefully
- No dangling references to readline symbols

---

## Future Considerations (Post-Master Merge)

### Vi Keybindings

When/if implemented:
- Modal editing (normal/insert/visual modes)
- `lle_keybinding_manager_load_vi_preset()`
- Mode indicator in prompt
- Full vim motion support (w, b, e, $, ^, etc.)
- Text objects (iw, aw, i", etc.)

### BSD Support

After macOS stability proven:
- Test on FreeBSD (most common)
- OpenBSD and NetBSD if interest exists
- Document platform-specific considerations

### Advanced Features

Ideas for future development:
- Abbreviations (fish-style)
- Directory jumping (z, autojump integration)
- Syntax highlighting for more languages in heredocs
- Right-side prompt support
- Async completion generation
- Plugin system for custom completers

---

## Progress Tracking

### Session Log

| Session | Date | Accomplishments |
|---------|------|-----------------|
| 35 | 2025-11-29 | Widget system, Unicode completion, RIGHT arrow fix |
| 36 | 2025-11-30 | Ctrl+G tiered dismissal, autosuggestion clearing |
| 37 | 2025-11-30 | Ctrl+G empty buffer fix, event pipeline documentation |
| 38 | 2025-11-30 | **P1 Complete**: Ctrl+C signal handling with LLE integration |

### Milestone Targets

- [x] **M1**: Ctrl+C working correctly with LLE (2025-11-30)
- [ ] **M2**: macOS build and test passing
- [ ] **M3**: Ctrl+R history search implemented
- [ ] **M4**: Undo/Redo implemented  
- [ ] **M5**: All P1-P4 complete - **Ready for master merge consideration**
- [ ] **M6**: Polish items (P5-P7) complete
- [ ] **M7**: Readline made optional

---

## Notes

- Always update this document when priorities change
- Session work should reference this roadmap
- Mark items complete with dates when done
- Add new items as discovered during development
