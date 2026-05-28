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

- Real-time syntax highlighting with kind-aware token classification
  (including path *shape* × *kind* coloring)
- Context-aware tab completions for every shell builtin and the
  `(lush-debug)` break-prompt vocabulary
- Emacs keybindings with kill ring and undo
- Multi-line editing with automatic continuation
- User customization trio: `display lle widget` (custom editing
  actions), `display lle hook` (lifecycle hooks), and `display lle
  segment` (prompt segments) — all routed through the central
  config registry

Inspired by the line editors in Zsh (ZLE) and Fish.

### Multi-Mode Architecture

Run scripts with different compatibility levels. `mode` is the
canonical builtin; `set -o posix` is a bash-bridge alias:

```bash
mode posix     # Strict POSIX sh compliance
mode bash      # Bash compatibility features
mode zsh       # Zsh compatibility features
mode lush      # Default mode - curated feature set
```

Modes are **presets, not restrictions** (see PHILOSOPHY.md §4):
selecting POSIX mode configures POSIX-conforming defaults, but lush
features (arrays, `[[ ]]`, debugger, process substitution) remain
available.

### Integrated Debugger

Debug shell scripts interactively — breakpoints anchored on real
source lines, depth-aware stepping, kind-aware variable inspection
(Scalar / List / Map), predictive type-mismatch warnings, and an
LLE-driven `(lush-debug)` break prompt. Not just `set -x` tracing.

```bash
debug on                         # Enable debugging
debug break add script.sh 15     # Set breakpoint
debug vars                       # Inspect variables (kind-aware)
debug analyze script.sh          # Static type/style/portability scan
source script.sh                 # Halts at line 15:
# (lush-debug) t arr             #   → arr: List (3 elements)
# (lush-debug) next               #   → depth-aware step over
# (lush-debug) continue
```

PHILOSOPHY.md §7 binds the debugger to keep pace with the language;
an integration-test gate enforces it.

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
- **Parameter expansion** - Case modification, substitution, slicing, bash transformations (`@Q`, `@E`, `@P`, `@a`), zsh parameter flags (`${(U)var}`, `${(o)arr}`, `${(k)m}`)
- **First-class value kinds** - Scalar / List / Map distinguished by the executor; no implicit list-to-string coercion (SEMANTICS §3.4); presentation operators (`[@]`, `[*]`, `[N]`) mandatory for list-in-scalar contexts (SEMANTICS §3.9)
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
meson compile -C build
./build/lush
```

The build directory MUST be named `build` (the project's meson
configuration assumes it). `-Werror` is enabled; verified clean on
macOS clang, Ubuntu gcc 13.x, and Fedora gcc 16.x.

### Test

```bash
meson test -C build
```

110 tests, zero memory leaks (verified with valgrind).

### Platforms

Linux (primary), macOS, BSD.

---

## Development Status

| Component | Status |
|-----------|--------|
| Core shell / POSIX builtins | Working |
| LLE — Emacs mode | Complete |
| LLE — Vi mode | Framework only |
| LLE customization (widget / hook / segment) | Complete |
| Extended tests `[[ ]]` | Complete |
| Brace expansion `{a,b}` `{1..10}` | Complete |
| Extended globbing `?(pat)` `*(pat)` | Complete |
| Parameter transformations `@Q` `@P` `@a` | Complete |
| Zsh parameter flags `${(U)var}` `${(o)arr}` | Working (single flag); composition WIP |
| Negative array indices `${arr[-1]}` | Complete |
| First-class List / Map storage (SEMANTICS §7) | Complete |
| No implicit list-to-string coercion (§3.4 / §3.9) | Complete |
| Shell modes | Working |
| Debugger — breakpoints, depth-aware stepping, kind-aware vars | Complete |
| Debugger — predictive type analysis (`debug analyze`) | Complete |
| Configuration system | Complete |
| Context-aware error system | Complete |
| Associative arrays | Complete |
| Advanced redirections | Complete |
| Arithmetic expansion | Complete |
| User extensibility / plugins | Not yet implemented |

The shell is functional for many use cases; the real-world script
scorecard (`meson test "real-world scorecard"`) is at 100% as of
v1.5.0 pre-release. Some edge cases remain.

---

## Documentation

- [Philosophy](docs/PHILOSOPHY.md) — design contracts (start here)
- [Vision](docs/VISION.md) — what lush is and why
- [Semantics](docs/SEMANTICS.md) — engine spec (value model, scoping)
- [User Guide](docs/USER_GUIDE.md) — feature reference
- [LLE Guide](docs/LLE_GUIDE.md) — line editor
- [Configuration](docs/CONFIGURATION.md) — modes, set, setopt, shopt, config
- [Debugger Guide](docs/DEBUGGER_GUIDE.md) — debugging
- [Builtin Commands](docs/BUILTIN_COMMANDS.md) — complete builtin reference
- [Changelog](docs/CHANGELOG.md) — version history

---

## License

MIT License. See [LICENSE](LICENSE).

---

**Lush** is a real shell, built from scratch, doing things differently.

It's not finished. But it's not vaporware either — it's a complete
POSIX engine, an integrated debugger held to a written contract,
110 tests, zero leaks, a real-world scorecard at 100%, and years of
development.

If you're curious about what a shell could be, lush is worth watching.
