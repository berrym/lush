# Lush

**A Unix shell with developer-first design.**

[![CI](https://github.com/berrym/lush/actions/workflows/ci.yml/badge.svg)](https://github.com/berrym/lush/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/berrym/lush/graph/badge.svg)](https://codecov.io/gh/berrym/lush)
[![Version](https://img.shields.io/badge/version-1.5.0--prerelease-blue)](https://github.com/berrym/lush/releases)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![C11](https://img.shields.io/badge/standard-C11-blue)](https://github.com/berrym/lush)

---

## What is Lush?

Lush is a Unix shell built entirely from scratch. It combines POSIX compliance with carefully chosen extensions from Bash and Zsh, plus capabilities found nowhere else—most notably an integrated debugger for shell scripts. The native line editor (LLE) provides syntax highlighting and context-aware completions without relying on GNU Readline or any external library. Lush is a single binary with zero runtime dependencies.

**Current status:** Under heavy development. Not yet suitable for daily use or production environments. Many features work well; others remain incomplete.

---

## Core Components

### LLE (Lush Line Editor)

A native line editor built specifically for lush:

- Real-time syntax highlighting with 45 token types
- Context-aware tab completions for all 50 shell builtins
- Emacs keybindings (44 actions) with kill ring and undo
- Multi-line editing with automatic continuation

Inspired by the line editors in Zsh (ZLE) and Fish.

### Multi-Mode Architecture

Run scripts with different compatibility levels:

```bash
set -o posix   # Strict POSIX sh compliance
set -o bash    # Bash compatibility features
set -o zsh     # Zsh compatibility features  
set -o lush  # Default mode - curated feature set
```

### Integrated Debugger

Debug shell scripts interactively - breakpoints, stepping, variable inspection. Not just `set -x` tracing.

```bash
debug on                         # Enable debugging
debug break add script.sh 15     # Set breakpoint
debug vars                       # Inspect variables
debug step                       # Step to next line
```

### Unified Configuration (v1.5.0)

TOML-based configuration with XDG Base Directory compliance:

```toml
# ~/.config/lush/lushrc.toml
[shell]
mode = "lush"

[display]
syntax_highlighting = true

[history]
size = 10000
```

The `setopt`/`unsetopt` commands provide Zsh-style option control. A central config registry keeps runtime state and configuration files synchronized.

---

## Extended Syntax

Lush implements extended shell features beyond POSIX:

- **Brace expansion** - `{a,b,c}` and `{1..10}` sequence expansion
- **Arrays** - Indexed arrays with negative index support (`${arr[-1]}`) and append syntax (`arr+=(x y)`)
- **Associative arrays** - Full support including literal syntax `declare -A map=([key]=value)`
- **Extended tests** - `[[ ]]` with pattern matching, regex, and file comparison (`-nt`, `-ot`, `-ef`)
- **Process substitution** - `<(cmd)` and `>(cmd)`
- **Parameter expansion** - Case modification, substitution, slicing, transformations (`@Q`, `@E`, `@P`, `@a`)
- **Extended globbing** - `?(pat)`, `*(pat)`, `+(pat)`, `@(pat)`, `!(pat)`
- **Advanced redirections** - Compound command redirections (`{ cmd; } > file`, `while ...; done < input`)
- **Hook functions** - `precmd`, `preexec`, `chpwd`, `periodic`

### Context-Aware Error System (v1.5.0)

Rust-style error reporting with source locations and suggestions:

```
error[E1001]: expected 'THEN', got 'FI'
  --> script.sh:5:10
   |
 5 | if true; fi
   |          ^~
   = while: parsing if statement
   = help: 'if' requires 'then' before 'fi'
```

Command-not-found errors include "did you mean?" suggestions using Unicode-aware fuzzy matching:

```
error[E1101]: gti: command not found
  --> <stdin>:1:1
   = help: did you mean 'git', 'gtail', or 'gtr'?
```

---

## Building

### Requirements

- C11 compiler (GCC 7+ or Clang 5+)
- Meson build system
- Ninja

### Build

```bash
git clone https://github.com/lush/lush.git
cd lush
meson setup build
ninja -C build
./build/lush
```

### Test

```bash
meson test -C build
```

57 tests, zero memory leaks (verified with valgrind).

### Platforms

Linux (primary), macOS, BSD.

---

## Development Status

| Component | Status |
|-----------|--------|
| Core shell / POSIX builtins | Working |
| LLE - Emacs mode | Complete |
| LLE - Vi mode | Framework only |
| Extended tests `[[ ]]` | Complete |
| Brace expansion `{a,b}` `{1..10}` | Complete |
| Extended globbing `?(pat)` `*(pat)` | Complete |
| Parameter transformations `@Q` `@P` `@a` | Complete |
| Negative array indices `${arr[-1]}` | Complete |
| Shell modes | Working |
| Debugger | Working |
| Configuration system | Complete |
| Context-aware error system | Complete |
| Associative arrays | Complete |
| Advanced redirections | Complete |
| Arithmetic expansion | Complete |
| User extensibility / plugins | Not yet implemented |

The shell is functional for many use cases. 82 extended syntax tests pass. Some edge cases remain.

---

## Documentation

- [User Guide](docs/USER_GUIDE.md) - Feature reference
- [LLE Guide](docs/LLE_GUIDE.md) - Line editor
- [Configuration](docs/CONFIGURATION.md) - Modes, set, setopt, shopt, config
- [Debugger Guide](docs/DEBUGGER_GUIDE.md) - Debugging
- [Builtin Commands](docs/BUILTIN_COMMANDS.md) - All 50 builtins
- [Changelog](docs/CHANGELOG.md) - Version history

---

## License

MIT License. See [LICENSE](LICENSE).

---

**Lush** is a real shell, built from scratch, doing things differently.

It's not finished. But it's not vaporware either - it's 50 builtins, 57 tests, zero leaks, and years of development.

If you're curious about what a shell could be, lush is worth watching.
