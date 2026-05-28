# Lush Documentation Index

**Version**: 1.5.0-prerelease
**The advanced interactive shell with LLE, multi-mode architecture,
typed functions, kind-tagged values, and an integrated debugger**

---

## Quick Start

| Document | Description |
|----------|-------------|
| [README](../README.md) | Project overview, features, quick start |
| [Getting Started](GETTING_STARTED.md) | First-time user guide |
| [User Guide](USER_GUIDE.md) | Complete feature reference |
| [Installation](INSTALLATION.md) | Build requirements, install, platform notes |
| [Changelog](CHANGELOG.md) | Version history and release notes |

---

## Foundational Documents

| Document | Description |
|----------|-------------|
| [Vision](VISION.md) | What lush is and why it exists |
| [Philosophy](PHILOSOPHY.md) | Founding principles -- identity vs polyglot, architectural correctness, the debugger-keeps-pace rule |
| [Semantics](SEMANTICS.md) | Engine spec: value kinds (scalar/list/map), scoping discipline, no implicit list-to-string |
| [Configuration](CONFIGURATION.md) | The four configuration surfaces: `mode`, `set`, `setopt`/`shopt`, `config` |

Start here before reading anything else. The three docs above give you
the *why* of every other decision in the repo.

---

## Feature Guides (`docs/features/`)

Focused user-facing guides for each major language / engine feature.
Each ~150-340 lines, structured: *What it is / Why it exists / Behavior /
Curated defaults / Gotchas*.

| Feature | Document |
|---------|----------|
| Typed functions (`fn name(p: kind) -> kind { ... }`) | [typed-functions.md](features/typed-functions.md) |
| Sigil conventions (`$`, `@`, `%` presentation contexts) | [sigil-conventions.md](features/sigil-conventions.md) |
| Pipeline status reporting (`PIPESTATUS`, `pipeline-diagnostic`) | [pipeline-status.md](features/pipeline-status.md) |
| Value inspector (`inspect-variable-at-cursor` widget) | [value-inspector.md](features/value-inspector.md) |
| Parameter-expansion error catalog + per-element semantics | [parameter-expansion-catalog.md](features/parameter-expansion-catalog.md) |

---

## Core Subsystems

### Line Editing (LLE)

| Document | Description |
|----------|-------------|
| [LLE Guide](LLE_GUIDE.md) | Complete LLE documentation -- buffer, cursor, history, keybindings, hooks, segments, widgets |

### Extended Syntax

| Document | Description |
|----------|-------------|
| [Extended Syntax](EXTENDED_SYNTAX.md) | Arrays, `[[]]`, process substitution, parameter expansion |
| [Advanced Scripting](ADVANCED_SCRIPTING_GUIDE.md) | Professional scripting with extended syntax |

### Completion System

| Document | Description |
|----------|-------------|
| [Completion System](COMPLETION_SYSTEM.md) | User-facing: LLE completions, context-aware builtin completions |
| [Completion Architecture](development/COMPLETION_ARCHITECTURE.md) | Developer reference: analyzer, sources, splicer, menu, how to add a source |

### Hook System

| Document | Description |
|----------|-------------|
| [Hooks and Plugins](HOOKS_AND_PLUGINS.md) | precmd, preexec, chpwd, periodic, hook arrays |

### Debugging

| Document | Description |
|----------|-------------|
| [Debugger Guide](DEBUGGER_GUIDE.md) | Breakpoints, stepping, kind-aware variable inspection, profiling, `debug analyze` static checks |

---

## Reference

### Commands

| Document | Description |
|----------|-------------|
| [Builtin Commands](BUILTIN_COMMANDS.md) | Complete builtin reference (~60 commands) |
| [Configuration](CONFIGURATION.md) | Mode preset selector, POSIX options, feature matrix, central registry |

### Comparison

| Document | Description |
|----------|-------------|
| [Feature Comparison](FEATURE_COMPARISON.md) | Lush vs Bash, Zsh, Fish, other shells |

### Project process

| Document | Description |
|----------|-------------|
| [Documentation Policy](DOCUMENTATION_POLICY.md) | How docs are organized, edited, and verified |
| [Comprehensive Test Suite](COMPREHENSIVE_TEST_SUITE.md) | Test categories, run instructions, coverage |
| [Release Changelog](CHANGELOG_RELEASE.md) | Release-notes-shaped changelog (vs CHANGELOG.md's detailed log) |

---

## Developer Documentation (`docs/development/`)

Implementation-detail documentation for contributors.

| Document | Description |
|----------|-------------|
| [Typed Functions Design](development/TYPED_FUNCTIONS_DESIGN.md) | Parser grammar, AST shapes, captured-scope mechanism |
| [Syntax Bridging Architecture](development/ARCHITECTURE-SYNTAX-BRIDGING.md) | Polyglot feature matrix, how `${var^^}` and `${(U)var}` reach one engine |
| [Spec Compatibility](development/SPEC-COMPATIBILITY.md) | Compatibility targets per shell |
| [Completion Architecture](development/COMPLETION_ARCHITECTURE.md) | Authoritative completion subsystem reference |
| [Parser Grammar Plan](development/PARSER_GRAMMAR_PLAN.md) | Parser implementation roadmap |
| [Fuzzing Plan](development/FUZZING_PLAN.md) | Four-phase fuzzing infrastructure |
| [Screen Buffer Specification](development/SCREEN_BUFFER_SPECIFICATION.md) | Virtual screen, cursor calculation, UTF-8 width handling |
| [LLE Display Architecture Research](development/LLE_DISPLAY_ARCHITECTURE_RESEARCH.md) | Clear-and-redraw model |
| [LLE Release Roadmap](development/LLE_RELEASE_ROADMAP.md) | LLE feature timeline |
| [Arena Memory Management Plan](development/ARENA_MEMORY_MANAGEMENT_PLAN.md) | LLE memory model |
| [Notification System Spec](development/NOTIFICATION_SYSTEM_SPEC.md) | Notification subsystem |
| [Expansion Audit](development/EXPANSION_AUDIT.md) | Expansion-engine review notes |
| [Dead Code Audit](development/DEAD_CODE_AUDIT.md) | Audit findings for unused / superseded code |
| [Test Quality Audit](development/TEST_QUALITY_AUDIT.md) | Test-coverage and quality findings |
| [Completion Rewrite Plan](development/COMPLETION_REWRITE_PLAN.md) | Historical: completion subsystem rewrite (postmortem reference) |

### LLE specification (`docs/lle_specification/`, `docs/lle_implementation/`)

Heavy implementer-level material for the Lush Line Editor subsystem.
Browse those subdirectories directly for spec-numbered reference (e.g.
`07_extensibility_framework_complete.md`).

---

## By Audience

### New Users

1. [Getting Started](GETTING_STARTED.md) - Installation, first steps, basic usage
2. [User Guide](USER_GUIDE.md) - Feature overview
3. [Vision](VISION.md) - What lush is and why
4. [LLE Guide](LLE_GUIDE.md) - Learn the line editor

### Daily Users

1. [User Guide](USER_GUIDE.md) - Feature reference
2. [Builtin Commands](BUILTIN_COMMANDS.md) - Command reference
3. [Configuration](CONFIGURATION.md) - Customize your shell
4. [Completion System](COMPLETION_SYSTEM.md) - Tab completion
5. [Feature Guides](features/) - Deep dives into the lush-specific features

### Script Writers

1. [Extended Syntax](EXTENDED_SYNTAX.md) - Arrays, tests, process substitution
2. [Advanced Scripting](ADVANCED_SCRIPTING_GUIDE.md) - Best practices
3. [Configuration](CONFIGURATION.md) - Mode presets and options
4. [Debugger Guide](DEBUGGER_GUIDE.md) - Debug your scripts
5. [Typed Functions](features/typed-functions.md) - First-class functions with kind discipline
6. [Sigil Conventions](features/sigil-conventions.md) - The `$`/`@`/`%` presentation contexts
7. [Semantics](SEMANTICS.md) - The value model your scripts run against

### Power Users

1. [Hooks and Plugins](HOOKS_AND_PLUGINS.md) - Customize shell behavior
2. [Configuration](CONFIGURATION.md) - Fine-tune options
3. [LLE Guide](LLE_GUIDE.md) - Master line editing
4. [Value Inspector](features/value-inspector.md) - Live variable inspection from the prompt
5. [Pipeline Status](features/pipeline-status.md) - Per-stage exit reporting

### Migrating Users

1. [Feature Comparison](FEATURE_COMPARISON.md) - Compare with your current shell
2. [Configuration](CONFIGURATION.md) - Choose a mode for existing scripts
3. [Getting Started](GETTING_STARTED.md) - Transition guide

### Contributors

1. [Philosophy](PHILOSOPHY.md) - Founding principles that govern every design call
2. [Semantics](SEMANTICS.md) - Engine spec
3. [Documentation Policy](DOCUMENTATION_POLICY.md) - How docs are organized
4. [Developer Documentation](#developer-documentation-docsdevelopment) - Implementation-detail material

---

## What's New in 1.5.0 (in progress)

The 1.5.0 cycle has shipped, among other things:

- **Typed functions** -- `fn name(p: kind) -> kind { ... }` with
  lexical scope and `let`-captured structured return values. See
  [typed-functions.md](features/typed-functions.md).
- **Sigil conventions** -- `$`, `@`, `%` for scalar / vector / pair
  presentation contexts. See [sigil-conventions.md](features/sigil-conventions.md).
- **Pipeline status reporting** -- `PIPESTATUS` / `pipestatus`
  populated unconditionally; `pipeline-diagnostic` mode surfaces
  per-stage structured errors. See
  [pipeline-status.md](features/pipeline-status.md).
- **Value inspector** -- `inspect-variable-at-cursor` widget for
  live variable inspection during line editing. See
  [value-inspector.md](features/value-inspector.md).
- **Parameter-expansion error catalog + per-element semantics**
  -- loud type mismatches at silent-no-op sites; element-wise
  `${arr[@]op}` and `${(flag)arr}` semantics. See
  [parameter-expansion-catalog.md](features/parameter-expansion-catalog.md).
- **`debug analyze`** static checks for typed-fn return-kind
  mismatches, sigil-kind misuse, and other edit-time diagnostics.

See [CHANGELOG.md](CHANGELOG.md) for the full per-session log and
[CHANGELOG_RELEASE.md](CHANGELOG_RELEASE.md) for the release-notes
view.

---

## Help Commands

```bash
help                    # General help
help <builtin>          # Specific builtin help
debug help              # Debugger help
display help            # Display system help
config show             # Configuration overview
debug analyze SCRIPT    # Static analysis of a script
```

---

## File Listing

### Root

| File | Description |
|------|-------------|
| `README.md` | Project overview |
| `LICENSE` | License information |

### docs/

| File | Description |
|------|-------------|
| `VISION.md` | What lush is and why |
| `PHILOSOPHY.md` | Founding principles |
| `SEMANTICS.md` | Engine spec (value kinds, scoping, no implicit coercion) |
| `CONFIGURATION.md` | The four configuration surfaces |
| `GETTING_STARTED.md` | First-time user guide |
| `USER_GUIDE.md` | Complete feature reference |
| `LLE_GUIDE.md` | Line editor documentation |
| `EXTENDED_SYNTAX.md` | Extended shell syntax |
| `COMPLETION_SYSTEM.md` | Completion documentation |
| `HOOKS_AND_PLUGINS.md` | Hook system |
| `DEBUGGER_GUIDE.md` | Debugging reference |
| `BUILTIN_COMMANDS.md` | Builtin reference |
| `INSTALLATION.md` | Installation guide |
| `ADVANCED_SCRIPTING_GUIDE.md` | Scripting guide |
| `FEATURE_COMPARISON.md` | Shell comparison |
| `CHANGELOG.md` | Detailed per-session changelog |
| `CHANGELOG_RELEASE.md` | Release-notes-shaped changelog |
| `COMPREHENSIVE_TEST_SUITE.md` | Test categories and coverage |
| `DOCUMENTATION_POLICY.md` | Doc organization and policy |
| `DOCUMENTATION_INDEX.md` | This file |

### docs/features/

| File | Description |
|------|-------------|
| `typed-functions.md` | Typed `fn` form, lexical scope, `let`-capture |
| `sigil-conventions.md` | `$`/`@`/`%` presentation sigils |
| `pipeline-status.md` | PIPESTATUS + pipeline-diagnostic mode |
| `value-inspector.md` | inspect-variable-at-cursor widget |
| `parameter-expansion-catalog.md` | Type-mismatch catalog + per-element semantics |

### docs/development/

See [the Developer Documentation section](#developer-documentation-docsdevelopment)
above for full descriptions. Subdirectory contains implementer-level
material: design docs, architecture references, audit notes, and
roadmaps.

### docs/lle_specification/ and docs/lle_implementation/

Heavy LLE subsystem reference. Browse directly for spec-numbered
files (`01_introduction.md`, `07_extensibility_framework_complete.md`,
etc.).
