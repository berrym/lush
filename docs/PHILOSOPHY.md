# Philosophy

**The founding principles that govern lush's design decisions.**

These are working contracts, not marketing slogans. Every architectural
choice in this project has been (or should be) measurable against them.
When a future contributor -- or a refreshed-after-time-away maintainer --
wonders "why is X this way?", the answer is somewhere in this document.

When in doubt, choose the option that follows these principles even if
it means more work today. Lush has the standing standard of architectural
correctness over expediency: scope expansion, signature changes, and full
subsystem rewrites are welcome when they're the right call.

---

## 1. Identity is curation + uniqueness, not the union of bash and zsh

Lush is **its own shell**. It is not bash, it is not zsh, it is not "the
shell that tries to be both and fails to be either."

Lush's identity comes from two sources:

1. **Curation.** When bash and zsh agree on a default, lush defaults to
   the consensus unless there is a principled reason to diverge. When
   they differ, lush curates one as default with the other available
   via configuration. The curation is opinionated and documented; it
   is never the result of "whichever was easier to implement."

2. **Uniqueness.** Lush ships features that no other shell has:
   integrated gdb-like debugger, rust-style structured errors with
   source citations, the layered display controller decoupled from the
   line editor (LLE), the unified central configuration registry that
   subsystems sync through, the syntax-bridging parser that maps
   bash and zsh dialects to a single canonical engine. These are not
   bonuses on top of "yet another POSIX shell"; they are part of the
   identity.

Lush has plenty of self-identity. The risk is not "no identity" -- the
risk is drifting toward "bash with extras" or "zsh with extras" because
those are the closest reference points. The standing reminder: lush is
not lesser than bash, not lesser than zsh, and not the union of the
two. It is its own coherent design.

---

## 2. Spelling is polyglot; behavior is canonical lush

Lush accepts bash and zsh spellings as a translation courtesy. It does
not adopt their architectures.

A bash user types `set -o pipefail`. A zsh user types `setopt
pipe_fail`. A lush user can type either. All three route to the same
canonical lush knob with one canonical implementation. Lush is a
polyglot shell because it understands the dialects, not because it
runs three engines under the hood.

This contract has a hard line:

- **Spelling**: lush recognizes bash and zsh syntax and option names
  where they have a clear semantic equivalent.
- **Behavior**: lush delivers the semantic the script writer
  intended, implemented with lush's coherent design.
- **Not promised**: byte-identical bash or zsh internals. If a script
  depends on `BASH_VERSION` being a specific string or on bash's
  exact word-splitting bug-for-bug, lush is allowed to differ. That
  is not a polyglot failure; the script is depending on
  implementation details rather than semantics.

The polyglot table is finite and maintained alongside the canonical
knobs. It is not free-form; it is a deliberate contract that grows
with demand and shrinks when bash or zsh deprecates an idiom.

---

## 3. Each configuration surface has one job and one canonical store

Lush has four configuration surfaces: `mode`, `set`, `setopt`/`shopt`,
and `config`. They are orthogonal:

- `mode` is identity selection (mutually-exclusive presets).
- `set` is POSIX shell options.
- `setopt` / `unsetopt` is the feature matrix; `shopt -s/-u` is the
  bash-spelling alias for the same operations.
- `config` is the central registry of per-knob persistent settings.

Each surface has one canonical store and does not share hidden state
with another. Aliases route through canonical state -- they do not
introduce duplicate sources of truth.

The dividing line between surfaces is articulated. A user knows where
to look for any given configuration concern via the decision tree in
`CONFIGURATION.md`. There is no hidden convention; the surfaces are
documented and discoverable.

This is the principle that prevents "muddy water" -- the situation
where the same setting can be changed via three different syntaxes
that interact in non-obvious ways. Lush's polyglot ambition makes
muddy water a constant temptation. The discipline is: one job per
surface, aliases route through canonical, every surface documented.

---

## 4. POSIX is a baseline lush respects but does not let restrict it

POSIX shell is a real and important compatibility target. Lush's POSIX
mode delivers strict POSIX compliance for scripts that need it.

POSIX is **not** a ceiling. It is a baseline. Lush's identity, its
curation, and its unique features are not constrained by what POSIX
permits. Lush extensions to POSIX semantics are added when they make
the shell better, not when bash or zsh did them first.

This means:

- POSIX mode is a preset, not the floor. Switching to POSIX mode
  applies POSIX-conforming defaults; lush features remain enabled
  because the user is in lush, not in `dash`.
- POSIX option names (`set -o errexit`, `set -o vi`, etc.) are the
  canonical spellings in lush's `set` builtin. Lush does not invent
  new spellings for things POSIX already names.
- `set -o posix` is recognized as a bash-bridge alias for `mode posix`
  because bash uses that idiom in scripts; the canonical lush
  spelling is `mode posix`.

POSIX compliance is a feature lush delivers, not a constraint on what
lush can be.

---

## 5. When bash and zsh agree, lush defaults match unless there's a principled reason

The simplest curation rule: when both bash and zsh default to the same
behavior for some knob, lush defaults to it too. The inertia of
existing scripts and existing user habits is real, and gratuitous
divergence is anti-polyglot.

When bash and zsh **disagree** on a default, lush curates one as
default and exposes the other via configuration. The curation has a
documented reason (the per-feature notes in `src/shell_mode.c` and
`CONFIGURATION.md`); it is never arbitrary.

When lush diverges from a bash/zsh consensus, the divergence has a
**principled lush-specific reason** -- not "I prefer it this way" but
"this default better serves the unique features of lush." Examples:

- `chain_directories` defaults to `true` in lush mode because lush's
  layered display + LLE integration makes the cascade behavior
  smooth and discoverable, while bash and zsh both default to single-
  tab-then-stop. The lush curation is fish-style discoverability for
  interactive use; the per-mode override ensures bash/zsh script
  contexts get the conventional behavior.
- `share_history` defaults to off in lush even though zsh has it on,
  because cross-session interference is confusing and lush's history
  subsystem already has good per-session ergonomics. The mode-specific
  default is documented; users can opt back in.

Each curated divergence is a deliberate choice with stated reasoning,
not a side-effect of implementation convenience.

---

## 6. Architectural correctness over expediency

This is the standing standard for the project. When two paths exist:

- Path A: small change, fits today's structure, doesn't address the
  underlying issue.
- Path B: larger change, restructures things, addresses the
  underlying issue.

The default is path B. Scope expansion is welcome when justified.
Signature changes, full subsystem rewrites, and breaking changes
(while pre-public) are appropriate when they make the architecture
right.

The corollary rules:

- **No bandaids.** A symptom-fix that leaves the root cause in place
  is not a fix; it is a defer.
- **No per-site copies of the same fix.** A pattern that needs fixing
  gets fixed at the level it lives; per-site duplication is a smell.
- **No test-side workarounds.** If a test exposes broken production
  code, the production code gets fixed. The test is a contract; it
  is not a place to paper over implementation defects.
- **Find the existing primitive before inventing one.** Lush is
  heavily designed; before proposing a new type, helper, parameter,
  or pattern, exhaustively explore the existing architecture.
- **Investigate first.** Thorough investigation before code changes;
  significant refactors are welcome when justified by the
  investigation.

The reason these are non-negotiable: they are the discipline that
keeps lush coherent over time. Without them, drift accumulates,
muddy water reappears, and the design loses the clarity that makes
this project worth building.

---

## 7. The debugger keeps pace with the language

Lush's integrated debugger is part of its identity (S1): "the IDE for
shell developers" is only true while the debugger understands
everything the shell can do. A debugger that has fallen behind the
language it debugs is worse than no debugger -- it misleads with
authority.

So the rule: no change to the language surface, the value model, or
observable execution semantics is complete until the debugger can
still see it. A new construct, a new value kind, a new scoping
discipline each carries a debugger obligation -- breakpoints still
halt within it, stepping still steps through it, inspection still
renders it truthfully. "Done" includes the debugger.

This cannot be a rule enforced by memory. Memory-enforced rules decay,
and this one already did: the debugger fell out of step with the
executor as the shell grew around it -- its line tracking, its
variable inspection, and the robustness of its break flow all decayed
-- because nothing forced the parallel work and nothing tested it. The
rule is therefore enforced by a gate -- an integration test suite that
drives the debugger over representative scripts and asserts it can set
breakpoints, halt, step, and inspect every value type. When a core
change outpaces the debugger, that test goes red; red CI is an
emergency, not a backlog item. The gate, not good intentions, holds
the rule.

The gate also scopes the rule honestly. A change the gate still passes
needs no debugger work. A change that reddens it is not done until the
debugger -- and the gate -- are green again.

The current gate is `tests/unit/test_debug_integration.c` (driven by
`meson test -C build`), with companion locks at `test_debug.c`,
`test_debug_trace.c`, `test_debug_breakpoints.c`, and
`test_debug_analysis.c`. As of this writing the debugger keeps pace
through Tier 2: line tracking on `node->loc.line` (no
command-ordinal counter); the break prompt is LLE-driven
(`lle_readline_no_history`) with history recall, ctrl-r search,
completion of the `(lush-debug)` command vocabulary, and a framed
left-gutter UI rendered through the screen buffer; variable
inspection queries the symtable directly via
`symtable_enumerate_current_scope_vars` and renders the actual
Scalar/List/Map kind; `type` / `t` exposes the kind explicitly;
`debug analyze` statically warns on the S3.9 list-in-scalar
pattern before the script runs. The two open obligations this rule
will impose work for next: a typed-function form (S8) and
lexical-scope resolution (S5.3); when those land they each carry a
debugger obligation by this rule.

---

## See also

- [VISION.md](VISION.md) -- project philosophy and design ambitions.
- [CONFIGURATION.md](CONFIGURATION.md) -- the four configuration
  surfaces, the decision tree, the per-mode-defaults mechanism.
- [development/ARCHITECTURE-SYNTAX-BRIDGING.md](development/ARCHITECTURE-SYNTAX-BRIDGING.md)
  -- syntax bridging design (the polyglot mechanism in concrete form).
- [development/SPEC-COMPATIBILITY.md](development/SPEC-COMPATIBILITY.md)
  -- compatibility targets and verification.
