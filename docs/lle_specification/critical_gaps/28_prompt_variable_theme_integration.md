# LLE Prompt Variable and Theme Integration Specification

**Document**: 28_prompt_variable_theme_integration.md
**Version**: 1.0.0
**Date**: 2026-02-22
**Status**: SPECIFICATION - Pre-implementation
**Dependencies**: Spec 25 (Prompt Theme System), Spec 13 (User Customization), Spec 26 (Adaptive Terminal Integration)
**Target**: Required for lush v1.5.0 release

---

## EXECUTIVE SUMMARY

The LLE prompt theme system currently bypasses traditional shell prompt variables (PS1, PS2, PROMPT), generating output independently and overwriting these variables as a side effect. This specification defines the architectural inversion required for v1.5.0: **PS1/PS2/PROMPT become the canonical format strings** through which all prompt configuration flows, with the theme system, TOML configuration, and user overrides all operating on these variables rather than around them.

**Core Principle**: Consistent with lush's superset shell philosophy, the prompt engine accepts bash-style escapes (`\u`, `\h`, `\w`), zsh-style percent escapes (`%n`, `%m`, `%~`), AND LLE segment syntax (`${directory}`, `${git}`) as different interfaces to the same underlying rendering engine. Syntax is polyglot; the engine is unified.

**Key Outcomes**:
- `PS1='[\u@\h \W]\$ '` works as expected (bash users feel at home)
- `PROMPT='%n@%m:%~%# '` works as expected (zsh users feel at home)
- `PS1='${user}@${host}:${directory}${?git: (${git})} ${symbol} '` uses LLE's full power
- TOML themes set PS1/PS2 via `[layout] ps1 = "..."` using any of the above syntaxes
- Explicit user PS1 is respected, not overridden by the theme system
- User-defined TOML themes are first-class citizens, indistinguishable from built-ins

---

## 1. ARCHITECTURAL OVERVIEW

### 1.1 Current Architecture (Problem)

```
Theme System ──► Composer ──► Segment Rendering ──► ANSI Output
                                    │
                                    └──► PS1/PS2 (overwritten as side effect)
                                              │
                                              └──► User sets PS1 ──► IGNORED next render
```

The prompt composer owns the rendering pipeline. PS1/PS2 are write-only outputs. User-set values are silently overwritten on every prompt render cycle.

### 1.2 Target Architecture (v1.5.0)

```
┌──────────────────────────────────────────────────────────┐
│                    PS1 / PS2 / PROMPT                     │
│              (canonical format strings)                    │
│                                                            │
│  Sources that SET these variables:                         │
│  ┌─────────────┐  ┌───────────┐  ┌───────────────────┐   │
│  │ Theme TOML  │  │ User      │  │ Shell script       │   │
│  │ [layout]    │  │ override  │  │ (lushrc, .bashrc)  │   │
│  │ ps1 = "..." │  │ PS1='...' │  │ export PS1='...'   │   │
│  └──────┬──────┘  └─────┬─────┘  └────────┬──────────┘   │
│         │               │                  │               │
│         └───────────────┴──────────────────┘               │
│                         │                                  │
│                    PS1 value                                │
│                         │                                  │
│              ┌──────────▼──────────┐                       │
│              │   Prompt Renderer   │                       │
│              │  (unified engine)   │                       │
│              │                     │                       │
│              │  Recognizes:        │                       │
│              │  • \u \h \w \W \$   │  ← bash escapes      │
│              │  • %n %m %~ %#      │  ← zsh escapes       │
│              │  • ${segment}       │  ← LLE segments      │
│              │  • ${?cond:t:f}     │  ← LLE conditionals  │
│              │  • %F{color}...%f   │  ← zsh color         │
│              └──────────┬──────────┘                       │
│                         │                                  │
│                    ANSI Output                             │
└──────────────────────────────────────────────────────────┘
```

### 1.3 Priority Order for PS1 Value

When determining what PS1 contains at render time:

1. **Explicit user override** (highest priority): If the user sets PS1 directly via command line, shell script, or `lushrc`, that value is used. The theme system does NOT overwrite it on subsequent renders.
2. **TOML theme configuration**: The active theme's `[layout] ps1` value is applied when a theme is activated or switched.
3. **Built-in theme default**: If no TOML override, the active built-in theme's ps1_format is used.
4. **Fallback**: `"$ "` (regular user) or `"# "` (root).

**Detection of user override**: Track whether PS1 was set by the theme system or by the user. A flag or generation counter in the symbol table entry distinguishes "theme-set" from "user-set". Theme switches update PS1 only if the current value was theme-set (or if the user explicitly requests a theme change).

---

## 2. UNIFIED PROMPT EXPANSION ENGINE

### 2.1 Supported Escape Syntaxes

The prompt renderer must recognize all three syntax families. When a format string is rendered, the engine processes it left-to-right, recognizing escapes from any family.

#### Bash-Style Escapes (backslash sequences)

| Escape | Expansion | Notes |
|--------|-----------|-------|
| `\u` | Username | `$USER` or `getpwuid()` |
| `\h` | Hostname (short) | Up to first `.` |
| `\H` | Hostname (full) | Complete hostname |
| `\w` | Working directory | `~` substitution for `$HOME` |
| `\W` | Basename of cwd | Last path component, `~` for home |
| `\d` | Date | `"Day Mon DD"` format |
| `\t` | Time (24h) | `HH:MM:SS` |
| `\T` | Time (12h) | `HH:MM:SS` |
| `\@` | Time (12h am/pm) | `HH:MM AM/PM` |
| `\A` | Time (24h short) | `HH:MM` |
| `\$` | Privilege indicator | `$` for regular, `#` for root |
| `\n` | Newline | Literal `\n` |
| `\r` | Carriage return | Literal `\r` |
| `\\` | Literal backslash | |
| `\[` | Begin non-printing | Marks start of ANSI escape for width calculation |
| `\]` | End non-printing | Marks end of ANSI escape |
| `\!` | History number | Current history event number |
| `\#` | Command number | Commands issued this session |
| `\j` | Job count | Number of background jobs |
| `\l` | Terminal name | Basename of terminal device |
| `\s` | Shell name | `"lush"` |
| `\v` | Shell version | Major.minor |
| `\V` | Shell version (full) | Major.minor.patch |
| `\e` | Escape character | `0x1B` |
| `\a` | Bell character | `0x07` |
| `\0NNN` | Octal character | Character by octal value |
| `\xNN` | Hex character | Character by hex value |

#### Zsh-Style Escapes (percent sequences)

| Escape | Expansion | Notes |
|--------|-----------|-------|
| `%n` | Username | Same as `\u` |
| `%m` | Hostname (short) | Same as `\h` |
| `%M` | Hostname (full) | Same as `\H` |
| `%d` or `%/` | Working directory (full) | Absolute path |
| `%~` | Working directory | `~` substitution |
| `%c` or `%.` | Basename of cwd | Same as `\W` |
| `%#` | Privilege indicator | `%` for regular, `#` for root |
| `%D{fmt}` | Date/time | strftime format string |
| `%T` | Time (24h) | `HH:MM` |
| `%t` or `%@` | Time (12h am/pm) | |
| `%*` | Time (24h with seconds) | `HH:MM:SS` |
| `%j` | Job count | Same as `\j` |
| `%l` | Terminal name | Same as `\l` |
| `%?` | Last exit code | Numeric |
| `%%` | Literal `%` | |
| `%B` / `%b` | Bold on / off | |
| `%U` / `%u` | Underline on / off | |
| `%S` / `%s` | Standout on / off | |
| `%F{color}` / `%f` | Foreground color / reset | Named, 256, or RGB |
| `%K{color}` / `%k` | Background color / reset | Named, 256, or RGB |
| `%(x.true.false)` | Conditional | Ternary based on condition `x` |
| `%N` | Truncation | `%N{width}...text` |

#### LLE Segment Syntax (dollar-brace sequences)

| Syntax | Expansion | Notes |
|--------|-----------|-------|
| `${segment}` | Render segment | e.g., `${directory}`, `${git}`, `${user}` |
| `${segment.property}` | Segment property | e.g., `${git.branch}`, `${directory.basename}` |
| `${?segment:true_text}` | Conditional | Show text only if segment is visible |
| `${?segment:true:false}` | Conditional with else | |
| `${color:text}` | Apply theme color | e.g., `${primary:hello}` |

**Available Segments**: `user`, `host`, `directory`, `git`, `time`, `status`, `jobs`, `symbol`

### 2.2 Syntax Disambiguation

The three syntaxes are unambiguous:
- `\` followed by a recognized escape letter → bash escape
- `%` followed by a recognized escape letter/pattern → zsh escape
- `${` → LLE segment/conditional syntax
- Literal `\`, `%`, `$` can be escaped: `\\`, `%%`, `\$`

**Edge case**: `${var}` in PS1 could be a shell variable expansion OR an LLE segment reference. Resolution: LLE segment names are a known fixed set (`user`, `host`, `directory`, `git`, `time`, `status`, `jobs`, `symbol`). If the name matches a known segment, treat it as a segment reference. Otherwise, treat it as a shell variable expansion (expand from environment/symbol table). This matches how zsh handles PROMPT expansion.

### 2.3 Expansion Order

1. **LLE segments** (`${segment}`, `${?cond:...}`) — expanded first, producing text that may contain ANSI codes
2. **Shell variable expansion** (`${VAR}` for non-segment names) — standard parameter expansion
3. **Prompt escapes** (`\u`, `%n`, etc.) — expanded to their values
4. **Non-printing markers** (`\[...\]`, `%{...%}`) — processed for width calculation

This order ensures that segments can produce color codes that are then properly wrapped in non-printing markers.

---

## 3. THEME SYSTEM AS PS1 CONFIGURATOR

### 3.1 Theme Activation Flow

When a theme is activated (startup, `theme` builtin, TOML config load):

```
1. Read theme's [layout] section
2. Set PS1 = theme.layout.ps1   (only if PS1 is currently theme-managed)
3. Set PS2 = theme.layout.ps2   (only if PS2 is currently theme-managed)
4. Store theme metadata for segment rendering
5. Mark PS1/PS2 as "theme-managed"
```

### 3.2 User Override Flow

When the user explicitly sets PS1:

```
1. User runs: PS1='[\u@\h \W]\$ '  (or export, or in lushrc)
2. Symbol table receives new PS1 value
3. Mark PS1 as "user-managed"
4. Theme system stops updating PS1 on subsequent renders
5. Prompt renderer uses the user's PS1 value directly
```

To return to theme-managed mode, the user can:
- Run a theme switch command: `theme powerline`
- Unset the override: `unset PS1` (reverts to theme default)
- Set in config: `prompt.use_theme = true` in lushrc.toml

### 3.3 TOML Theme Configuration (Starship-Inspired)

Theme TOML files define prompt format strings that become PS1/PS2 values. The syntax supports all three escape families, but LLE segment syntax is the recommended approach for new themes.

```toml
# ~/.config/lush/themes/my-theme.toml

[theme]
name = "my-theme"
description = "My custom prompt"
version = "1.0.0"
category = "custom"

[layout]
# LLE segment syntax (recommended - cross-terminal, full-featured)
ps1 = "${user}@${host}:${directory}${?git: (${git})} ${symbol} "
ps2 = "> "

# Bash-style escapes also work
# ps1 = "[\u@\h \W]\$ "

# Zsh-style escapes also work
# ps1 = "%n@%m:%~%# "

# Mixed syntax works (lush is a superset)
# ps1 = "\u@\h:${directory}${?git: (${git})} \$ "

[colors]
primary = "#5FAFFF"
secondary = "#87D787"
success = "#5FFF00"
warning = "#FFAF00"
error = "#FF0000"
info = "#00FFFF"
git_clean = "#5FFF00"
git_dirty = "#FFAF00"

[symbols]
prompt = "$"
prompt_root = "#"
branch = ""

[syntax]
keyword = { color = "#5FAFFF", bold = true }
command_valid = { color = "#5FFF00", bold = true }
string = "#FFAF00"
variable = "#D787FF"
comment = { color = "#808080", dim = true }
```

### 3.4 Segment Configuration in TOML

Individual segments can be configured per-theme, controlling visibility, format, and color:

```toml
[segments]
enabled = ["user", "host", "directory", "git", "status", "jobs", "symbol"]

[segments.directory]
truncation_length = 3       # Show last N path components
home_symbol = "~"           # Symbol for home directory
style = "short"             # full, short, basename

[segments.git]
show_branch = true
show_status = true
show_ahead_behind = true
show_stash = false
truncation_length = 20      # Max branch name length

[segments.time]
format = "%H:%M"            # strftime format
style = "24h"               # 24h, 12h, timestamp
```

---

## 4. PROMPT VARIABLE SEMANTICS

### 4.1 Variable Definitions

| Variable | Purpose | Render Context |
|----------|---------|----------------|
| `PS1` | Primary prompt | Before each command |
| `PS2` | Continuation prompt | Multi-line input continuation |
| `PS3` | Select prompt | `select` command menu |
| `PS4` | Debug trace prefix | `set -x` trace output |
| `PROMPT` | Alias for PS1 | Zsh compatibility |
| `RPROMPT` | Right-side prompt | Right-aligned on PS1 line (future) |
| `PROMPT_COMMAND` | Pre-prompt hook | Bash: command executed before PS1 display |
| `precmd` | Pre-prompt hook | Zsh: function called before PS1 display |

### 4.2 PROMPT as PS1 Alias

For zsh compatibility, `PROMPT` is a bidirectional alias for `PS1`:
- Setting `PROMPT` sets `PS1` (and vice versa)
- Reading `PROMPT` reads `PS1`
- Theme system treats them as identical

### 4.3 PS3 and PS4

PS3 and PS4 are simpler variables that support bash/zsh prompt escapes but NOT LLE segment syntax (segments are irrelevant for select menus and trace output). They are not managed by the theme system.

---

## 5. TOML CONFIGURATION INTEGRATION

### 5.1 Configuration Hierarchy

Settings are resolved in priority order (highest first):

1. **Runtime shell variable override**: `PS1='...'` at the command line
2. **User theme file**: `~/.config/lush/themes/<name>.toml` `[layout] ps1`
3. **User config**: `~/.config/lush/lushrc.toml` `[prompt] format`
4. **System theme**: `/etc/lush/themes/<name>.toml`
5. **Built-in theme**: Compiled-in theme definitions
6. **Default fallback**: `"$ "` / `"# "`

### 5.2 Config File Integration

In `lushrc.toml`:

```toml
[prompt]
# Select active theme (loads from theme registry)
theme = "powerline"

# OR set prompt format directly (overrides theme's ps1)
# format = "${directory}${?git: (${git})} $ "

# Whether theme system manages PS1/PS2
# Set to false to fully manage PS1 yourself
use_theme = true

# Git integration for prompt segments
git_enabled = true
git_cache_timeout = 5

# Terminal color handling
auto_detect_colors = true
```

### 5.3 User-Defined Theme Parity

User themes loaded from TOML files MUST be treated identically to built-in themes:

- Registered in the same theme registry
- Listed alongside built-ins in `theme list` output (with `[user]` source tag)
- Support full inheritance from any theme (built-in or user)
- Support all configuration sections (colors, symbols, syntax, segments, layout)
- Hot-reloadable without shell restart (`theme reload`)
- Exportable/shareable as single `.toml` files

---

## 6. IMPLEMENTATION PHASES

### Phase 1: Unified Prompt Expansion Engine

**Goal**: Build a single rendering function that accepts PS1 format strings with all three escape syntaxes and produces ANSI terminal output.

**Files**:
- New: `src/lle/prompt/prompt_expansion.c` / `include/lle/prompt/prompt_expansion.h`
- Refactor from: `src/executor.c` `transform_prompt()` (bash escapes), `src/lle/prompt/template_engine.c` (LLE segments)

**Deliverables**:
- `lle_prompt_expand(const char *format, char *output, size_t output_size, lle_prompt_context_t *ctx)` — unified expansion function
- Handles all escapes from Section 2.1
- Uses segment rendering callbacks from existing template engine
- Terminal-capability-aware color output (via cached detection)

### Phase 2: PS1 as Canonical Format String

**Goal**: Invert the prompt rendering flow so PS1 is read, expanded, and rendered rather than being overwritten.

**Files**:
- Modify: `src/lle/prompt/composer.c` — read PS1 instead of generating from scratch
- Modify: `src/lle/lle_shell_integration.c` — set PS1 from theme on activation, respect user overrides
- Modify: `src/display/prompt_layer.c` — use expanded PS1 for display

**Deliverables**:
- Theme activation sets PS1/PS2 (one-time, not every render)
- Prompt render reads PS1, expands it, displays it
- User override tracking (theme-managed vs user-managed flag)
- `PROMPT` as bidirectional alias for PS1

### Phase 3: TOML Theme Configuration Completeness

**Goal**: Ensure TOML theme files can fully configure every aspect of the prompt experience.

**Files**:
- Modify: `src/lle/prompt/theme_parser.c` — complete parsing for all sections
- Modify: `src/lle/prompt/theme_loader.c` — complete loading for all fields
- Add: Per-segment configuration in `[segments.<name>]` sections
- Verify: `examples/theme.toml` works end-to-end

**Deliverables**:
- All `[~] parsed but not used` items from `examples/theme.toml` become fully functional
- Per-segment configuration (truncation, format, visibility)
- Right prompt (`rps1` / `RPROMPT`) rendering support
- Transient prompt support
- Theme hot-reload (`theme reload`)

### Phase 4: User Theme Parity and Polish

**Goal**: User-defined themes are fully equivalent to built-in themes. The system is ready for v1.5.0.

**Files**:
- Modify: `src/lle/prompt/theme.c` — ensure user themes have full feature parity
- Modify: `src/builtins/builtins.c` — `theme` builtin for list/switch/reload
- Add: Theme validation and helpful error messages

**Deliverables**:
- `theme list` shows all themes with source tags
- `theme <name>` switches themes (updates PS1/PS2)
- `theme reload` hot-reloads user theme files
- `theme export <name>` exports current settings as TOML
- Theme validation with actionable error messages
- Comprehensive test coverage

---

## 7. COMPATIBILITY CONSIDERATIONS

### 7.1 Bash Compatibility

Users with existing `.bashrc` that sets PS1 should have their prompt work correctly:
```bash
# In .bashrc or lushrc:
PS1='[\u@\h \W]\$ '
# Result: [mberry@fedora lush]$
```

The theme system detects this as a user override and does not interfere.

### 7.2 Zsh Compatibility

Users with zsh-style PROMPT settings should work:
```zsh
# In .zshrc or lushrc:
PROMPT='%n@%m:%~%# '
# Result: mberry@fedora:~/Lab/c/lush%
```

### 7.3 Starship/Oh-My-Posh Users

Users who want to use external prompt generators:
```toml
# In lushrc.toml:
[prompt]
use_theme = false
external_prompt = "starship prompt"
```

This disables the built-in theme system entirely and defers to the external tool. PS1 is set by the external tool's output.

### 7.4 Migration Path

For users upgrading to v1.5.0:
- Default behavior: Built-in theme manages PS1/PS2 (unchanged from current)
- Existing TOML theme configs continue to work
- Users who set PS1 explicitly will now have their choice respected
- No breaking changes for users who don't set PS1 manually

---

## 8. TESTING REQUIREMENTS

### 8.1 Unit Tests

- Bash escape expansion: all escapes from Section 2.1 table
- Zsh escape expansion: all escapes from Section 2.1 table
- LLE segment expansion: all segments and conditionals
- Mixed syntax expansion: combinations of all three families
- Edge cases: empty PS1, very long PS1, nested escapes, malformed escapes

### 8.2 Integration Tests

- Theme activation sets PS1 correctly
- User PS1 override is respected across multiple prompt renders
- Theme switch after user override asks or restores correctly
- TOML theme load → PS1 value matches `[layout] ps1`
- `PROMPT` alias bidirectionality

### 8.3 Compliance Tests

- All bash prompt escapes produce correct output (verified against bash)
- All zsh prompt escapes produce correct output (verified against zsh)
- User-defined themes load and render identically to built-in themes
- Theme inheritance resolves correctly for user themes inheriting from built-ins

---

## 9. RELATIONSHIP TO EXISTING SPECIFICATIONS

| Spec | Relationship |
|------|-------------|
| **Spec 25** (Prompt Theme System) | This spec refines Spec 25's architecture by making PS1 the canonical interface. Spec 25's template engine, segment system, and theme registry remain; the change is in how they connect. |
| **Spec 13** (User Customization) | This spec extends Spec 13 by defining the TOML schema for complete theme customization and ensuring user themes have full built-in parity. |
| **Spec 26** (Adaptive Terminal) | Color output in prompt expansion uses the unified terminal detection from Spec 26. |
| **Core Shell** | The prompt expansion engine reuses the existing `transform_prompt()` logic from `executor.c` but extends it to handle zsh and LLE syntaxes. |

---

## 10. SUCCESS CRITERIA FOR v1.5.0

- [ ] PS1/PS2 are the canonical prompt format strings (not bypassed)
- [ ] All bash prompt escapes work correctly
- [ ] All zsh prompt escapes work correctly
- [ ] LLE segment syntax works in PS1
- [ ] TOML themes set PS1 via `[layout] ps1`
- [ ] Explicit user PS1 is never silently overwritten
- [ ] User-defined TOML themes are functionally identical to built-in themes
- [ ] `theme` builtin supports list, switch, reload
- [ ] All existing tests continue to pass
- [ ] New test suites for prompt expansion and theme integration

---

**Last Updated**: 2026-02-22
**Primary Integration Target**: Spec 25 (Prompt Theme System)
