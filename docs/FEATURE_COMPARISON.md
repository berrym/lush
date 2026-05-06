# Feature Comparison

**How Lush compares to other shells**

---

## Table of Contents

1. [Overview](#overview)
2. [Feature Matrix](#feature-matrix)
3. [Detailed Comparisons](#detailed-comparisons)
4. [Migration Guide](#migration-guide)
5. [When to Use What](#when-to-use-what)

---

## Overview

Lush v1.4.0 occupies a unique position in the shell landscape:

- **Native line editor (LLE)** - Not readline, not ZLE, built from scratch
- **Multi-mode architecture** - Run POSIX, Bash, Zsh, or Lush mode
- **Integrated debugging** - No other shell has this
- **Modern features** - Hook system, extended syntax, context-aware completion

This document compares Lush with Bash, Zsh, Fish, and Dash.

---

## Feature Matrix

### Core Features

| Feature | Lush | Bash | Zsh | Fish | Dash |
|---------|:------:|:----:|:---:|:----:|:----:|
| POSIX compliance | Yes | Yes | Yes | No | Yes |
| Interactive use | Excellent | Good | Excellent | Excellent | Minimal |
| Scripting | Advanced | Excellent | Excellent | Different | Fast |
| Performance | Fast | Good | Moderate | Fast | Very Fast |

### Line Editing

| Feature | Lush | Bash | Zsh | Fish | Dash |
|---------|:------:|:----:|:---:|:----:|:----:|
| Line editor | LLE (native) | Readline | ZLE | Native | None |
| Emacs mode | Yes (44 actions) | Yes | Yes | Partial | No |
| Vi mode | In development | Yes | Yes | Yes | No |
| Syntax highlighting | Yes (45 types) | No | Plugin | Yes | No |
| Context-aware completion | Yes (45 builtins) | Limited | Plugin | Yes | No |
| Multi-line editing | Yes | Yes | Yes | Yes | No |
| History search | Yes | Yes | Yes | Yes | No |

### Extended Syntax

| Feature | Lush | Bash | Zsh | Fish | Dash |
|---------|:------:|:----:|:---:|:----:|:----:|
| Indexed arrays | Yes | Yes | Yes | Yes | No |
| Associative arrays | Yes | Yes | Yes | No | No |
| `[[]]` extended test | Yes | Yes | Yes | No | No |
| `=~` regex match | Yes | Yes | Yes | No | No |
| Process substitution | Yes | Yes | Yes | Yes | No |
| Extended globbing | Yes | Yes | Yes | Yes | No |
| Glob qualifiers | Yes | No | Yes | No | No |
| `;&` case fall-through | Yes | Yes | Yes | No | No |
| `select` loop | Yes | Yes | Yes | No | No |
| Nameref variables | Yes | Yes | Yes | No | No |

### Hook System

| Feature | Lush | Bash | Zsh | Fish | Dash |
|---------|:------:|:----:|:---:|:----:|:----:|
| precmd | Yes | No | Yes | Yes | No |
| preexec | Yes | No | Yes | Yes | No |
| chpwd | Yes | No | Yes | Yes | No |
| periodic | Yes | No | Yes | No | No |
| Hook arrays | Yes | No | Yes | No | No |

### Debugging

| Feature | Lush | Bash | Zsh | Fish | Dash |
|---------|:------:|:----:|:---:|:----:|:----:|
| Integrated debugger | **Yes** | No | No | No | No |
| Breakpoints | Yes | No | No | No | No |
| Step execution | Yes | No | No | No | No |
| Variable inspection | Yes | No | No | No | No |
| Profiling | Yes | No | No | No | No |
| `set -x` tracing | Yes | Yes | Yes | No | Yes |

### Configuration

| Feature | Lush | Bash | Zsh | Fish | Dash |
|---------|:------:|:----:|:---:|:----:|:----:|
| Config command | Yes | No | No | Yes | No |
| Startup files | Yes | Yes | Yes | Yes | Yes |
| Theme system | Yes | No | Plugin | No | No |
| Shell modes | Yes | No | Partial | No | No |

---

## Detailed Comparisons

### Lush vs Bash

**Lush advantages:**
- Native line editor with syntax highlighting
- Context-aware completion for all builtins
- Integrated debugging (unique feature)
- Hook system (precmd, preexec, chpwd, periodic)
- Glob qualifiers from Zsh
- Shell modes for compatibility

**Bash advantages:**
- Universal availability
- Massive documentation ecosystem
- Mature completion system
- Broader plugin ecosystem
- More tested edge cases

**Migration notes:**
- Most Bash scripts work in Bash mode
- Some Bash-specific extensions may differ
- Arrays, `[[]]`, process substitution all work
- Use `set -o bash` for compatibility

### Lush vs Zsh

**Lush advantages:**
- Simpler configuration (no Oh-My-Zsh needed)
- Integrated debugging (unique feature)
- Native syntax highlighting (no plugin)
- Works out of the box
- Multi-mode for compatibility

**Zsh advantages:**
- More extensive glob qualifiers
- Larger plugin ecosystem
- More configuration options
- Longer history of edge case handling
- More completion definitions

**Migration notes:**
- Hook system is compatible (precmd, preexec, etc.)
- Glob qualifiers work in Zsh/Lush mode
- Use `set -o zsh` for compatibility
- Most Zsh scripts work

### Lush vs Fish

**Lush advantages:**
- POSIX compatible syntax
- Bash/Zsh script compatibility
- Integrated debugging (unique feature)
- Traditional shell scripting
- Multi-mode architecture

**Fish advantages:**
- Designed for interactive use
- Simpler syntax (but not POSIX)
- Better out-of-box autosuggestions
- Extensive web-based configuration
- Better default experience

**Migration notes:**
- Fish scripts need rewriting (different syntax)
- Interactive habits transfer well
- Completion is similar concept
- Syntax highlighting is similar

### Lush vs Dash

**Lush advantages:**
- Interactive features
- Extended syntax
- Debugging
- Modern editing
- Everything Dash doesn't have

**Dash advantages:**
- Fastest startup
- Smallest footprint
- Pure POSIX (better for /bin/sh)
- Simpler (fewer features = fewer bugs)

**Migration notes:**
- POSIX scripts work in POSIX mode
- Dash is for scripts, Lush for development
- Use Lush to debug, Dash to run

---

## Migration Guide

### From Bash

1. **Test your scripts:**
   ```bash
   lush --bash script.sh
   ```

2. **Set mode in scripts:**
   ```bash
   #!/usr/bin/env lush
   set -o bash
   ```

3. **Gradually adopt features:**
   - Add debugging where needed
   - Use LLE features interactively
   - Add hooks for customization

### From Zsh

1. **Test your scripts:**
   ```bash
   lush --zsh script.sh
   ```

2. **Migrate configuration:**
   - Hooks work the same way
   - Theme system is different
   - Plugins need adaptation

3. **Check glob qualifiers:**
   - Common ones work: `*(.)`, `*(/)`, `*(@)`
   - Some Zsh-specific ones may differ

### From Fish

1. **Rewrite scripts:**
   - Fish syntax is not compatible
   - Use POSIX or Bash syntax

2. **Adapt habits:**
   - Tab completion works similarly
   - Syntax highlighting is similar
   - History search is similar

3. **Benefits gained:**
   - Script portability
   - Debugging capability
   - Standard shell syntax

---

## When to Use What

### Use Lush When

- **Developing shell scripts** - The debugger changes everything
- **Need Bash/Zsh compatibility** - Multi-mode handles both
- **Want modern editing** - LLE with syntax highlighting
- **Interactive debugging** - Breakpoints, stepping, inspection
- **Hook-based workflows** - precmd, preexec, chpwd, periodic

### Use Bash When

- **Maximum compatibility** - It's everywhere
- **Existing Bash-specific scripts** - Some edge cases differ
- **Established workflows** - Migration takes effort

### Use Zsh When

- **Heavy plugin use** - Larger ecosystem
- **Complex Zsh configurations** - Already invested
- **Obscure Zsh features** - Some aren't in Lush

### Use Fish When

- **Pure interactive focus** - That's Fish's specialty
- **Non-POSIX is okay** - Different syntax
- **Web configuration** - Fish's unique feature

### Use Dash When

- **Minimum footprint** - /bin/sh scripts
- **Fastest execution** - Dash wins
- **Pure POSIX** - No extensions needed

---

## Summary

Lush's unique position:

| Capability | Unique to Lush |
|------------|------------------|
| LLE line editor | Native, not readline |
| Integrated debugger | No other shell has this |
| Multi-mode | POSIX/Bash/Zsh/Lush in one |
| Context-aware completions | 45 builtins with full completion |
| Real-time syntax highlighting | 45 token types, built-in |
| Hook system with arrays | Zsh-compatible, in a new shell |

The integrated debugger alone makes Lush worth using for anyone who writes shell scripts. The rest is the modern shell experience that should have existed years ago.

---

## See Also

- [USER_GUIDE.md](USER_GUIDE.md) - Complete feature reference
- [LLE_GUIDE.md](LLE_GUIDE.md) - Line editor details
- [CONFIGURATION.md](CONFIGURATION.md) - Mode documentation
- [DEBUGGER_GUIDE.md](DEBUGGER_GUIDE.md) - Debugging features
