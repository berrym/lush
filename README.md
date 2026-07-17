# Lush

**A modern, polyglot Unix shell built from scratch — with its own ideas about what a shell should be.**

[![CI](https://github.com/berrym/lush/actions/workflows/ci.yml/badge.svg)](https://github.com/berrym/lush/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/berrym/lush/graph/badge.svg)](https://codecov.io/gh/berrym/lush)
[![Version](https://img.shields.io/badge/version-0.3.0--dev-orange)](https://github.com/berrym/lush)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![C11](https://img.shields.io/badge/standard-C11-blue)](https://github.com/berrym/lush)

---

## What is Lush?

Lush is a Unix shell written from scratch in C11, with no dependency on GNU
Readline or any external runtime library — a single binary.

It is **polyglot**: POSIX, bash, and zsh syntax are treated as different ways
to reach the *same* underlying engine, not as separate compatibility modes
bolted on. `${var^^}` and `${(U)var}` both uppercase a string because they
compile to the same operation.

It is also **its own shell**. Lush honors the shells that came before it, but
it is not a clone of any of them and is not afraid to diverge from them when
that means fixing a long-standing problem rather than reproducing it. Some of
what it does — a real integrated script debugger, a schema-driven
configuration system, a first-class value model that refuses silent
list-to-string coercion — no other shell does.

> **Status: early-stage (`0.3.0-dev`), under active development.**
> Lush is capable and genuinely fun to use today, but it is **not** yet a
> production daily driver, and nothing here should be read as *proven*
> complete. It is honest work in progress. Testers, feedback, and
> contributors are welcome — this is an interesting project to poke at and
> help shape.

---

## What makes it different

- **One polyglot engine, not three emulators.** Bash and zsh spellings map
  onto shared operations. `mode bash` / `mode zsh` load presets, but the
  features stay available regardless of mode — profiles are presets, not
  restrictions.
- **First-class value kinds.** Scalar / List / Map are distinguished by the
  executor. There is no implicit list-to-string coercion; a vector-yielding
  expansion must occupy its whole word, so how a value prints depends on the
  `[@]` / `[*]` / `[N]` you ask for, never on quoting accidents
  (SEMANTICS §3).
- **An integrated debugger**, not `set -x`. Breakpoints on real source lines,
  depth-aware stepping, kind-aware variable inspection, and a predictive
  static analyzer — held to a written contract that requires it to keep pace
  with the language.
- **A native line editor (LLE).** Real-time syntax highlighting, context-aware
  completion, multi-line editing, and a customization surface (widgets, hooks,
  prompt segments) — built for lush, not inherited from Readline.
- **A configuration nervous system.** A schema-first, reactive registry keeps
  runtime state and config files in sync, validates writes, and drives layered
  precedence and provenance.
- **Errors that help.** Rust-style diagnostics with source locations,
  context, and suggestions — including "did you mean?" for unknown commands.

---

## A closer look

### LLE — the Lush Line Editor

A native line editor built specifically for lush:

- Real-time syntax highlighting with kind-aware token classification
  (including path *shape* × *kind* coloring)
- Context-aware tab completion for builtins and the `(lush-debug)`
  break-prompt vocabulary
- Emacs keybindings with kill ring and undo (a vi-mode framework exists but
  is not complete)
- Multi-line editing with automatic continuation
- User customization: `display lle widget` (editing actions),
  `display lle hook` (lifecycle hooks), and `display lle segment`
  (prompt segments), all routed through the config registry

### Polyglot modes

`mode` is the canonical builtin; `set -o posix` is a bash-bridge alias for it.

```bash
mode posix     # POSIX-conforming defaults
mode bash      # bash-flavored defaults
mode zsh       # zsh-flavored defaults
mode lush      # the curated default
```

Modes are **presets, not restrictions** (PHILOSOPHY §4): POSIX mode sets
POSIX-conforming defaults, but lush features (arrays, `[[ ]]`, the debugger,
process substitution) remain reachable.

### Integrated debugger

```bash
debug on                         # enable debugging
debug break add script.sh 15     # set a breakpoint
debug analyze script.sh          # static type / style / portability scan
source script.sh                 # halts at line 15:
# (lush-debug) t arr             #   -> arr: List (3 elements)
# (lush-debug) next              #   -> depth-aware step over
# (lush-debug) continue
```

Breakpoints anchor on real source lines; inspection is kind-aware
(Scalar / List / Map) with predictive type-mismatch warnings. PHILOSOPHY §7
binds the debugger to the language via an integration-test gate.

### Configuration

TOML with XDG Base Directory compliance, backed by the config registry:

```toml
# ~/.config/lush/lushrc.toml
[shell]
mode = "lush"

[display]
syntax_highlighting = true

[history]
size = 10000
```

`setopt` / `unsetopt` provide zsh-style option control; `config` inspects and
edits registry-backed settings directly. The registry validates writes and
rejects bad values with a targeted diagnostic.

### Extended syntax

Beyond POSIX, lush implements:

- **Brace expansion** — `{a,b,c}`, `{1..10}`
- **Arrays** — indexed with negative indices (`${arr[-1]}`) and append
  (`arr+=(x y)`)
- **Associative arrays** — including literal `declare -A map=([key]=value)`
- **Extended tests** — `[[ ]]` with pattern matching, regex, and file
  comparisons (`-nt`, `-ot`, `-ef`)
- **Process substitution** — `<(cmd)`, `>(cmd)`
- **Parameter expansion** — case modification, substitution, slicing, bash
  transformations (`@Q`, `@E`, `@P`, `@a`), and zsh parameter flags
  (`${(U)var}`, `${(o)arr}`, `${(k)m}`)
- **Extended globbing** — `?(pat)`, `*(pat)`, `+(pat)`, `@(pat)`, `!(pat)`
- **Compound-command redirections** — `{ cmd; } > file`,
  `while ...; done < input`
- **Hook functions** — `precmd`, `preexec`, `chpwd`, `periodic`

### Diagnostics

```
error[E1001]: expected 'THEN', got 'FI'
  --> script.sh:5:10
   |
 5 | if true; fi
   |          ^~
   = while: parsing if statement
   = help: 'if' requires 'then' before 'fi'
```

Unknown commands get Unicode-aware fuzzy "did you mean?" suggestions:

```
error[E1101]: gti: command not found
  --> <stdin>:1:1
   = help: did you mean 'git', 'gtail', or 'gtr'?
```

---

## Building

**Requirements:** a C11 compiler (GCC 7+ or Clang 5+), Meson, and Ninja.

```bash
git clone https://github.com/berrym/lush.git
cd lush
meson setup build
meson compile -C build
./build/lush
```

The build directory must be named `build` (the Meson configuration assumes
it). `-Werror` is enabled; the tree builds clean on macOS clang and Ubuntu
gcc.

```bash
meson test -C build     # 176 tests across unit, integration, and real-world suites
```

**Platforms:** Linux and macOS are exercised regularly; BSD is intended but
less tested.

---

## How it is tested

Correctness is pursued with more than unit tests:

- A **mode-aware differential harness** runs lush and the matching reference
  shell (dash / bash / zsh) on the same input and compares them. It is a *gap
  finder*: each divergence is judged against lush's own philosophy — a bug
  and internal inconsistency get fixed, while a deliberate, documented
  difference is kept. Matching another shell byte-for-byte is not the goal.
- **Coverage-guided fuzzers** exercise the tokenizer, parser, and executor.
- CI builds under **AddressSanitizer + UBSan**; the LLE suites are enforced
  with leak detection on. Making the remaining unit-suite tests leak-clean and
  enforcing the same gate there is tracked work (issue #506), not a finished
  claim.

---

## Status at a glance

Read "Working" as "implemented and exercised by tests," not "proven complete
for every edge case."

| Area | Status |
|------|--------|
| Core shell / POSIX builtins | Working |
| Polyglot modes (posix / bash / zsh / lush) | Working |
| First-class Scalar / List / Map value model | Working |
| No implicit list-to-string coercion (SEMANTICS §3) | Working |
| Arrays, associative arrays, negative indices | Working |
| Extended tests `[[ ]]`, brace expansion, extended globbing | Working |
| Parameter transformations (`@Q` `@P` `@a`) | Working |
| Zsh parameter flags (`${(U)var}`, `${(o)arr}`) | Working (single flag); composition in progress |
| Process substitution, compound-command redirections | Working |
| Arithmetic expansion | Working |
| LLE — Emacs editing, highlighting, completion, customization | Working |
| LLE — vi mode | Framework only |
| Integrated debugger — breakpoints, stepping, kind-aware vars, `debug analyze` | Working |
| Configuration registry | Working |
| Context-aware error/diagnostic system | Working |
| User extensibility / plugins | Not yet implemented |

The differential real-world scorecard (`meson test "real-world scorecard"`)
currently passes over the curated corpus, and the differential harness reports
no unexpected divergences. Edge cases and rough surfaces remain — that is what
the `0.x` line is for.

---

## Documentation

- [Philosophy](docs/PHILOSOPHY.md) — the founding design contracts (start here)
- [Vision](docs/VISION.md) — what lush is and why
- [Semantics](docs/SEMANTICS.md) — the engine spec (value model, scoping)
- [User Guide](docs/USER_GUIDE.md) — feature reference
- [LLE Guide](docs/LLE_GUIDE.md) — the line editor
- [Configuration](docs/CONFIGURATION.md) — `mode`, `set`, `setopt`/`shopt`, `config`
- [Debugger Guide](docs/DEBUGGER_GUIDE.md) — debugging shell scripts
- [Builtin Commands](docs/BUILTIN_COMMANDS.md) — the builtin reference

Documentation is being audited and brought current alongside the code; if a
doc and the shell disagree, trust the shell and please report it.

---

## Contributing

Lush is early-stage and moving. If a shell that tries to be genuinely better —
not just another POSIX clone — sounds interesting, try it, break it, and open
an issue. Bug reports with a minimal reproduction are especially valuable, and
the philosophy and semantics docs above explain the design constraints a
change is expected to respect.

---

## License

MIT License. See [LICENSE](LICENSE).

---

Lush is a real shell, built from scratch, deliberately doing some things
differently. It is not finished and does not pretend to be — but it is a
working polyglot engine with an integrated debugger, a native line editor, and
a coherent design it is willing to be judged against. If you are curious about
what a shell *could* be, it is worth a look.
