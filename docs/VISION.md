# Lush Vision

**Author**: Michael Berry  
**Status**: Foundational - Do Not Modify Without Executive Decision

---

## What Lush Is

Lush is a **superset shell**. It is not an emulator. It does not pretend to be bash or zsh. It is its own shell with its own opinions, designed to understand and encompass the capabilities of traditional shells while charting its own path.

## Core Principles

### 1. Lush Has Its Own Opinion

The lush profile preset represents a curated, opinionated set of defaults - how a shell *should* work out of the box, informed by years of experience with both bash and zsh. These defaults are not arbitrary; they are deliberate choices about the best behaviors from across the shell ecosystem.

### 2. Syntax Is Polyglot

Lush accepts bash syntax and zsh syntax because they are just different ways to express the same underlying operations. `shopt -s extglob` and `setopt extended_glob` both enable the same feature. `${var^^}` and `${(U)var}` both uppercase a string. The syntax is an interface layer; the feature engine is unified.

This is not about compatibility. It is about recognizing that shell users come from different backgrounds and should not be forced to abandon familiar syntax to use a better shell.

### 3. Behavior Is Configurable

Everything routes through lush's central configuration system. Every feature, every default, every behavior can be tuned. The profile presets (posix, bash, zsh, lush) are starting points, not restrictions. Users customize freely.

### 4. Scripts Mostly Work

Because lush's feature set encompasses what other shells do, most scripts written for bash or zsh run without modification. This is a natural consequence of being a superset, not a design goal that compromises lush's identity.

### 5. When Scripts Don't Work, Lush Knows Why

This is the game changer. Lush understands shell differences better than the shells themselves. The compatibility database, static analyzer, and debugger can identify:

- What shell a script was written for
- Which features it uses that have different semantics across shells  
- What lush does differently and why
- How to achieve the intended behavior in lush

Lush does not just run scripts. It comprehends them.

## What Lush Is Not

- **Not a bash clone** - Lush does not aim for bug-for-bug bash compatibility
- **Not a zsh clone** - Lush does not replicate zsh's quirks
- **Not a POSIX-only shell** - POSIX compliance is a floor, not a ceiling
- **Not an emulator** - Lush never pretends to be another shell

## The Lush Way

Lush does not write different tokenizers, parsers, symbol tables, or executors to match other shells. Lush is rich enough that its components encompass the older shells. When a component's design proves inadequate, it gets rewritten to be better - not to match some other shell's limitations.

If at any point it becomes clear that an architecture or component cannot support this vision, starting over is not feared. Getting it right matters more than preserving existing code.

## Value Proposition

Lush is for shell users who:

- Work across multiple systems with different default shells
- Maintain scripts written for various shells
- Want one shell that speaks all the dialects
- Need tooling that understands shell differences
- Appreciate opinionated defaults but demand configurability

Lush respects the old while embracing new paths. It does not mind breaking norms when something of real use is gained.

---

## See also

- [PHILOSOPHY.md](PHILOSOPHY.md) -- the founding principles that
  govern day-to-day design decisions: identity vs polyglot, surface
  separation, POSIX as baseline not restriction, architectural
  correctness over expediency.
- [CONFIGURATION.md](CONFIGURATION.md) -- the four configuration
  surfaces and how they relate.

---

*This document defines what lush is. All development decisions should align with this vision. Changes to this vision require explicit executive decision by the project owner.*
