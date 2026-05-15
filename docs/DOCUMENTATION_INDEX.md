# Lush Documentation Index

**Version**: 1.4.0  
**The advanced interactive shell with LLE, multi-mode architecture, and integrated debugging**

---

## Quick Start

| Document | Description |
|----------|-------------|
| [README](../README.md) | Project overview, features, quick start |
| [Getting Started](GETTING_STARTED.md) | First-time user guide |
| [User Guide](USER_GUIDE.md) | Complete feature reference |
| [Changelog](../CHANGELOG.md) | Version history and release notes |

---

## Core Features

### Line Editing (LLE)

| Document | Description |
|----------|-------------|
| [LLE Guide](LLE_GUIDE.md) | Complete LLE documentation |
| Emacs keybindings | Full reference in LLE Guide |
| Vi mode | In development - see LLE Guide |
| Syntax highlighting | See LLE Guide |

### Configuration

| Document | Description |
|----------|-------------|
| [Configuration](CONFIGURATION.md) | The four configuration surfaces: `mode`, `set`, `setopt`/`shopt`, `config` -- including POSIX modes, shell options, and the registry |

### Extended Syntax

| Document | Description |
|----------|-------------|
| [Extended Syntax](EXTENDED_SYNTAX.md) | Arrays, `[[]]`, process substitution, parameter expansion |
| [Advanced Scripting](ADVANCED_SCRIPTING_GUIDE.md) | Professional scripting with extended syntax |

### Completion System

| Document | Description |
|----------|-------------|
| [Completion System](COMPLETION_SYSTEM.md) | User-facing: LLE completions, context-aware builtin completions |
| [Completion Architecture](development/COMPLETION_ARCHITECTURE.md) | Developer reference: analyzer, sources, splicer, menu, configuration, how to add a source |

### Hook System

| Document | Description |
|----------|-------------|
| [Hooks and Plugins](HOOKS_AND_PLUGINS.md) | precmd, preexec, chpwd, periodic, hook arrays |

### Debugging

| Document | Description |
|----------|-------------|
| [Debugger Guide](DEBUGGER_GUIDE.md) | Breakpoints, stepping, variable inspection, profiling |

---

## Reference

### Commands

| Document | Description |
|----------|-------------|
| [Builtin Commands](BUILTIN_COMMANDS.md) | All 48 shell builtins |
| [Configuration](CONFIGURATION.md) | Mode preset selector, POSIX options, feature matrix, central registry |

### Other reference

| Document | Description |
|----------|-------------|
| [Installation](INSTALLATION.md) | Build requirements, installation, platform notes |

### Comparison

| Document | Description |
|----------|-------------|
| [Feature Comparison](FEATURE_COMPARISON.md) | Lush vs Bash, Zsh, Fish, other shells |

---

## By Audience

### New Users

1. [Getting Started](GETTING_STARTED.md) - Installation, first steps, basic usage
2. [User Guide](USER_GUIDE.md) - Feature overview
3. [LLE Guide](LLE_GUIDE.md) - Learn the line editor

### Daily Users

1. [User Guide](USER_GUIDE.md) - Feature reference
2. [Builtin Commands](BUILTIN_COMMANDS.md) - Command reference
3. [Configuration](CONFIGURATION.md) - Customize your shell
4. [Completion System](COMPLETION_SYSTEM.md) - Tab completion

### Script Writers

1. [Extended Syntax](EXTENDED_SYNTAX.md) - Arrays, tests, process substitution
2. [Advanced Scripting](ADVANCED_SCRIPTING_GUIDE.md) - Best practices
3. [Configuration](CONFIGURATION.md) - Mode presets and options
4. [Debugger Guide](DEBUGGER_GUIDE.md) - Debug your scripts

### Power Users

1. [Hooks and Plugins](HOOKS_AND_PLUGINS.md) - Customize shell behavior
2. [Configuration](CONFIGURATION.md) - Fine-tune options
3. [LLE Guide](LLE_GUIDE.md) - Master line editing

### Migrating Users

1. [Feature Comparison](FEATURE_COMPARISON.md) - Compare with your current shell
2. [Configuration](CONFIGURATION.md) - Choose a mode for existing scripts
3. [Getting Started](GETTING_STARTED.md) - Transition guide

---

## What's New in v1.4.0

### Major Features

- **LLE (Lush Line Editor)**: Native line editor replacing readline
  - Complete Emacs keybindings (42 actions)
  - Context-aware completions for all 45 builtins
  - Real-time syntax highlighting (30+ token types)
  - Vi mode framework (wiring in progress)

- **Multi-Mode Architecture**: Run in the mode you need
  - POSIX mode for strict compliance
  - Bash mode for bash script compatibility
  - Zsh mode for zsh script compatibility
  - Lush mode (default) with all features

- **Extended Language Support**: Modern shell syntax
  - Arrays (indexed and associative)
  - Arithmetic: `(())`, `let`
  - Extended test: `[[]]`, `=~` regex
  - Process substitution: `<()`, `>()`
  - Extended parameter expansion
  - Extended globbing and glob qualifiers
  - Control flow: `;&`, `;;&`, `select`, `time`

- **Hook System**: Zsh-style hooks
  - `precmd`, `preexec`, `chpwd`, `periodic`
  - Hook arrays for multiple handlers

### Documentation

New documents for v1.4.0:

| Document | Content |
|----------|---------|
| [LLE Guide](LLE_GUIDE.md) | Complete LLE reference |
| [Extended Syntax](EXTENDED_SYNTAX.md) | Extended language features |
| [Configuration](CONFIGURATION.md) | Modes, options, registry |
| [Hooks and Plugins](HOOKS_AND_PLUGINS.md) | Hook system |

---

## Help Commands

```bash
help                    # General help
help <builtin>          # Specific builtin help
debug help              # Debugger help
display help            # Display system help
config show             # Configuration overview
```

---

## File Listing

### Root

| File | Description |
|------|-------------|
| `README.md` | Project overview |
| `CHANGELOG.md` | Release notes |
| `LICENSE` | License information |

### docs/

| File | Description |
|------|-------------|
| `GETTING_STARTED.md` | First-time user guide |
| `USER_GUIDE.md` | Complete feature reference |
| `LLE_GUIDE.md` | Line editor documentation |
| `EXTENDED_SYNTAX.md` | Extended shell syntax |
| `CONFIGURATION.md` | Modes, set, setopt/shopt, config -- all four configuration surfaces |
| `COMPLETION_SYSTEM.md` | Completion documentation |
| `HOOKS_AND_PLUGINS.md` | Hook system |
| `DEBUGGER_GUIDE.md` | Debugging reference |
| `BUILTIN_COMMANDS.md` | Builtin reference |
| `INSTALLATION.md` | Installation guide |
| `ADVANCED_SCRIPTING_GUIDE.md` | Scripting guide |
| `FEATURE_COMPARISON.md` | Shell comparison |
| `CHANGELOG.md` | Detailed changelog |
| `DOCUMENTATION_INDEX.md` | This file |
