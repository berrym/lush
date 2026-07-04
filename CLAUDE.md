# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure and build (build directory MUST be named "build")
meson setup build && meson compile -C build

# Rebuild after changes
meson compile -C build

# Run the shell
./build/lush

# Run all tests
meson test -C build

# Run specific test suite
meson test -C build test_buffer_operations
meson test -C build -v  # verbose
```

## Project Vision

Lush is a **superset shell**. It is not an emulator. It accepts both bash and zsh syntax because they are different ways to express the same underlying operations. The syntax is an interface layer; the feature engine is unified.

**Core Principles**:
- **Syntax is polyglot**: `${var^^}` and `${(U)var}` both uppercase a string
- **Profiles are presets, not restrictions**: `mode bash` (or its alias `set -o bash`) loads defaults, but all features remain available
- **When scripts don't work, lush knows why**: Compatibility database, static analyzer, debugger
- **Value kinds are first-class**: Scalar / List / Map are distinguished by the executor; no implicit list-to-string coercion (SEMANTICS §3.4 / §3.9)
- **The debugger keeps pace with the language**: PHILOSOPHY §7 enforces this via an integration-test gate (`tests/unit/test_debug_integration.c`)

Lush does NOT write different tokenizers/parsers/executors to match other shells. Lush is rich enough that its components encompass the older shells.

### Architectural gate: curate, don't clone (ENFORCES the vision above and PHILOSOPHY §1–2)

lush is its own shell — not bash-with-extras, not zsh-with-extras. PHILOSOPHY §1 ("Identity is curation + uniqueness, not the union of bash and zsh") and §2 ("Spelling is polyglot; behavior is canonical lush") are LAWS, not preferences. The recurring failure mode is drifting toward bash because bash is the nearest reference point. This gate exists because the law was stated but never operationalized.

Run this gate BEFORE writing code, choosing a behavior, or launching any review:

1. **Do bash and zsh AGREE here?** If yes → the consensus is the lush default (PHILOSOPHY §1.1); that is curation, not cloning. If they DIFFER → lush curates ONE as default with a **documented rationale**, the other reachable via config — NEVER "whichever was easier / whichever I checked first."
2. **PHILOSOPHY §2: lush does NOT promise byte-identical bash internals.** Do not replicate a legacy quirk (a shell's exact retention/display/edge behavior) as though it were a spec.
3. **Am I building a clean lush primitive, or copying behavior?**

If a solution matches a single shell by default rather than by deliberate, documented curation — **HALT and redesign.**

Reviews are gated too: an adversarial pass may **not** use one shell's output as "the oracle." bash, zsh, and POSIX are three references; the review question is "is this lush-curated per PHILOSOPHY §1.1," never "does it match bash."

## Architecture Overview

### Core Shell Pipeline

```
Input → Tokenizer → Parser → Executor → Output
         (lexical)   (AST)    (interpret)
```

**Key files**:
- `src/tokenizer.c` - Lexical analysis, 80+ token types, two-token lookahead
- `src/parser.c` - Recursive descent parser, builds AST
- `src/executor.c` - Tree-walking interpreter, command dispatch
- `src/builtins/builtins.c` - 60+ builtin commands
- `include/node.h` - AST node types (NODE_COMMAND, NODE_PIPE, NODE_IF, etc.)

**Execution flow**:
```c
parser_t *parser = parser_new_with_source(input, "<stdin>");
node_t *ast = parser_parse(parser);
executor_execute(executor, ast);
```

### Shell Modes & Syntax Bridging

Lush implements **syntax bridging** where multiple syntaxes map to the same underlying feature:

```
shopt -s extglob     →  FEATURE_EXTENDED_GLOB  ←  setopt extended_glob
${var^^}             →  CASE_UPPER             ←  ${(U)var}
```

**Feature matrix** in `src/shell_mode.c`:
- 42 features across 4 profiles (POSIX, Bash, Zsh, Lush)
- `shell_mode_allows(FEATURE_X)` - query if feature enabled
- `shell_feature_enable/disable()` - runtime override
- `setopt`, `unsetopt`, `shopt` all operate on same underlying flags

### LLE (Lush Line Editor)

Native line editor replacing GNU Readline. Buffer-oriented, event-driven architecture.

**13 modules** in `src/lle/`:
| Module | Purpose |
|--------|---------|
| `core/` | Error handling, memory, performance, arena allocators |
| `unicode/` | UTF-8 support, grapheme detection, character width |
| `buffer/` | Command buffer, cursor management, change tracking |
| `event/` | Event system and queue (async-ready) |
| `terminal/` | Terminal abstraction, capability detection |
| `input/` | Input parsing, key detection, escape sequences |
| `display/` | Display integration, render pipeline |
| `history/` | History storage, search, deduplication |
| `multiline/` | Multiline editing, continuation detection |
| `keybinding/` | Key mappings, Emacs bindings, kill ring |
| `completion/` | Tab completion system |
| `prompt/` | Prompt templates, themes |
| `adaptive/` | Terminal detection and adaptation |

**Key principle**: LLE is the **single source of truth** for buffer content and cursor position. The display system queries LLE; it never modifies LLE state.

**Customization surfaces (LLE Phase 3, shipped)**:
- `display lle widget` — user-defined editing actions bound to key sequences
- `display lle hook` — lifecycle hook registration (precmd, preexec, chpwd, periodic)
- `display lle segment` — custom prompt segments backed by shell variables

**Debug break prompt**: the `(lush-debug)` prompt is LLE-driven
via `lle_readline_no_history` (separate in-process history,
swap-restore on entry/exit). First-word completion switches to the
debug command vocabulary when `lle_in_debug_prompt()` is true.

### Display System

**CRITICAL: Lush does NOT use differential/diff-based display updates.**

The display system uses **prompt-once, clear-and-redraw**:
1. Prompt is drawn ONCE on first render
2. Every subsequent update: clear from prompt position, redraw everything
3. Cursor repositioned after redraw

Many spec documents reference differential updates - **ignore those references**. The current working implementation is simpler.

**Key components** in `src/display/`:
- `display_controller.c` - Orchestrates render cycle
- `screen_buffer.c` - Virtual screen representation for cursor calculation
- `command_layer.c` - Receives command text from LLE, applies syntax highlighting

**Render cycle**:
```c
// 1. Get content from layers
command_layer_get_highlighted_text(layer, buffer, size);

// 2. Render to virtual screen (calculates cursor position)
screen_buffer_render(&buffer, prompt, command, cursor_offset);

// 3. Output to terminal (clear + redraw)
// Cursor position from buffer.cursor_row, buffer.cursor_col
```

**Screen buffer** handles:
- UTF-8 character width (CJK = 2 columns)
- ANSI escape sequence skipping
- Line wrapping at terminal width
- Cursor position tracking through all complexity

### Configuration System

TOML-based configuration with XDG compliance:
- Primary: `~/.config/lush/lushrc.toml`
- Shell script: `~/.config/lush/lushrc` (sourced after TOML)

**Config registry** (`src/config.c`):
- Pub/sub pattern for configuration changes
- Type-safe access with change notifications
- Sections: shell, history, completion, prompt, display, behavior

## Code Standards

- **C11 standard** with strict warnings
- **Function prefixes**: `lle_` for LLE, `screen_buffer_` for screen buffer, etc.
- **Error handling**: Return `lle_result_t` (LLE_SUCCESS, LLE_ERROR_*)
- **Memory**: Arena allocators and memory pools where possible
- **No memory leaks**: Valgrind clean

## Git Standards

- **No emojis** in commit messages
- **No attribution lines** (no Co-Authored-By - git handles attribution)
- Professional, descriptive messages
- Format: `<type>: <description>`

## Key Files Reference

### Core Shell
| File | Purpose |
|------|---------|
| `src/lush.c` | Main entry point, REPL loop |
| `src/tokenizer.c` | Lexical analysis |
| `src/parser.c` | AST construction |
| `src/executor.c` | Command execution |
| `src/shell_mode.c` | Feature matrix, profile system |
| `src/config.c` | Configuration registry |

### LLE
| File | Purpose |
|------|---------|
| `src/lle/lle_editor.c` | Main line editor |
| `src/lle/lle_readline.c` | Readline API compatibility |
| `src/lle/buffer/buffer_management.c` | Core buffer operations |
| `src/lle/keybinding/keybinding_actions.c` | Editing actions, cursor movement |
| `src/lle/history/history_core.c` | History management |

### Display
| File | Purpose |
|------|---------|
| `src/display/display_controller.c` | Render cycle orchestration |
| `src/display/screen_buffer.c` | Virtual screen, cursor calculation |
| `src/display/command_layer.c` | Command text + syntax highlighting |
| `src/display/prompt_layer.c` | Prompt rendering |

## Testing

40+ test suites organized by category:
- `lle-unit` - Unit tests for LLE modules
- `lle-functional` - Functional tests (buffer ops, history, multiline)
- `lle-integration` - Integration tests
- `lle-compliance` - Spec compliance verification

Run specific categories:
```bash
meson test -C build -v test_buffer_operations
meson test -C build -v test_history_phase1
```

## Important Documentation

- `docs/VISION.md` - Project philosophy (read first)
- `docs/PHILOSOPHY.md` - Founding principles governing day-to-day design decisions (identity vs polyglot, surface separation, architectural correctness over expediency, debugger-keeps-pace rule §7)
- `docs/SEMANTICS.md` - Engine spec: value model (Scalar / List / Map), scoping discipline, no implicit list-to-string coercion, the engine-vs-preset distinction
- `docs/CONFIGURATION.md` - The four configuration surfaces (`mode`, `set`, `setopt`/`shopt`, `config`) -- authoritative reference replacing the prior SHELL_MODES / CONFIG_SYSTEM / SHELL_OPTIONS docs
- `docs/development/CONFIG_NERVOUS_SYSTEM.md` - North-star vision + proven architecture for CREG as the central configuration nervous system (schema-first, reactive bindings, layered precedence + provenance); the strangler-migration roadmap. Read before changing the config registry
- `docs/BUILTIN_COMMANDS.md` - Complete builtin reference (~60 commands; canonical inventory)
- `docs/DEBUGGER_GUIDE.md` - Integrated debugger: (lush-debug) prompt, breakpoints, kind-aware inspection, depth-aware stepping, `debug analyze` predictive type warnings
- `docs/development/ARCHITECTURE-SYNTAX-BRIDGING.md` - Syntax bridging design
- `docs/development/COMPLETION_ARCHITECTURE.md` - Completion subsystem reference (analyzer, sources, splicer, menu, how to add a source). Authoritative; supersedes spec 12
- `docs/development/SPEC-COMPATIBILITY.md` - Compatibility targets
- `docs/lle_specification/LLE_DESIGN_DOCUMENT.md` - LLE architecture
- `docs/development/SCREEN_BUFFER_SPECIFICATION.md` - Screen buffer details
