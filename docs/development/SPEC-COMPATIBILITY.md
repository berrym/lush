# Lush Compatibility Specification

**Status**: APPROVED
**Date**: 2026-01-12
**Decisions Made**: Session 119

**Reading note (2026-05-23):** the four-surface configuration model
named here has since been codified in two canonical documents that
take precedence on conflict: [`PHILOSOPHY.md`](../PHILOSOPHY.md)
sections 2-5 (identity, polyglot, configuration surfaces, POSIX-as-
baseline) and [`CONFIGURATION.md`](../CONFIGURATION.md) (the `mode`,
`set`, `setopt`/`shopt`, `config` surfaces with the decision tree).
The value model that scripts cross when "shell profiles" change is
specified in [`SEMANTICS.md`](../SEMANTICS.md). Treat this document
as the original architectural decisions and verification targets;
treat the canonical docs as the current normative reference.

---

## Executive Summary

Lush implements **Pragmatic Compatibility (Level 2+)** with bash and zsh through:

1. **Shell Profiles** - `set -o bash|zsh|lush|posix` loads a profile (preset defaults)
2. **Syntax Bridging** - Both bash and zsh syntax available in ALL modes
3. **User Choice** - Any feature can be customized after loading a profile
4. **Unified Config** - All settings flow through lush's config registry

Lush is NOT an emulator. It's a modern shell that speaks multiple dialects.

---

## Architectural Decisions (Finalized)

### Decision 1: Option C - Universal Syntax Support

**Both bash and zsh syntax are available in ALL modes.**

Rationale:
- User choice is non-negotiable in lush's design philosophy
- Syntax is just an interface; underlying implementation is unified
- Features are additive, not mode-gated
- Shell profile is a preset, not a restriction

Examples:
```bash
# These ALL work in ANY mode:
echo ${var^^}       # Bash syntax for uppercase
echo ${(U)var}      # Zsh syntax for uppercase - SAME result

shopt -s extglob    # Bash syntax to enable extglob
setopt extended_glob # Lush/zsh syntax - SAME effect

declare -n ref=var  # Bash nameref syntax
typeset -n ref=var  # Zsh nameref syntax - SAME effect
```

### Decision 2: Terminology - "Shell Profile" not "Shell Mode"

The term "mode" implies emulation. What lush actually provides is **profiles**:

| Command | Meaning |
|---------|---------|
| `set -o bash` | Load bash profile (bash defaults) |
| `set -o zsh` | Load zsh profile (zsh defaults) |
| `set -o lush` | Load lush profile (curated best-of-both) |
| `set -o posix` | Load POSIX profile (strict compliance) |

After loading a profile:
- User can modify ANY setting via `setopt`/`unsetopt`
- User can persist customizations via `config save`
- The profile is just the starting point

### Decision 3: Separate Execution Pipelines Per Feature

Each feature has its own clear execution path, guarded by the feature matrix. This:
- Protects POSIX compliance when in POSIX profile
- Allows features to be individually toggled
- Prevents feature interactions from breaking core functionality

### Decision 4: Single Parser, Multiple Syntaxes

Lush maintains ONE parser that recognizes multiple syntax forms:
- No separate "bash parser" or "zsh parser"
- Parser handles syntax translation internally
- Underlying feature implementation is unified

### Decision 5: Honest Limitations

Lush will:
- Clearly document what it does and doesn't support
- Never claim to be bash or zsh
- Never obscure limitations from users
- Provide accurate compatibility metrics

---

## Syntax Bridging Architecture

### Principle

Syntax is an interface layer. Multiple syntaxes map to the same underlying operations.

### Builtin Bridging

```
┌─────────────────────────────────────────────────────────┐
│                    User Input                           │
│  shopt -s extglob  │  setopt extended_glob              │
└─────────┬──────────┴─────────────┬──────────────────────┘
          │                        │
          ▼                        ▼
┌─────────────────────────────────────────────────────────┐
│              Builtin Dispatcher                         │
│  Recognizes: shopt, setopt, declare, typeset, etc.     │
└─────────────────────┬───────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────┐
│              Feature Matrix                             │
│  shell_feature_enable(FEATURE_EXTENDED_GLOB)           │
└─────────────────────────────────────────────────────────┘
```

### Parameter Expansion Bridging

```
┌─────────────────────────────────────────────────────────┐
│                    User Input                           │
│     ${var^^}       │      ${(U)var}                     │
└─────────┬──────────┴─────────────┬──────────────────────┘
          │                        │
          ▼                        ▼
┌─────────────────────────────────────────────────────────┐
│              Parser                                     │
│  Recognizes both syntaxes, creates same AST node       │
└─────────────────────┬───────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────┐
│              Executor                                   │
│  expand_case_modification(var, CASE_UPPER)             │
└─────────────────────────────────────────────────────────┘
```

---

## Feature Matrix Integration

The feature matrix (`shell_feature_t` enum) remains the source of truth for what's enabled.

### Profile Loading

When `set -o bash` is executed:
1. Reset all features to their bash-profile defaults
2. These defaults match bash 5.x behavior
3. User overrides are NOT cleared (or optionally cleared with `--reset`)

### User Customization

```bash
set -o bash                    # Load bash profile
setopt null_glob               # Override: enable nullglob (bash has it off)
config set shell.null_glob on  # Same thing via config registry
config save                    # Persist for future sessions
```

---

## Syntax Support Matrix

### Currently Implemented

| Feature | Bash Syntax | Zsh Syntax | Lush Native | Status |
|---------|-------------|------------|---------------|--------|
| Enable option | `shopt -s X` | `setopt X` | `setopt X` | **TODO: shopt** |
| Disable option | `shopt -u X` | `unsetopt X` | `unsetopt X` | **TODO: shopt** |
| Nameref | `declare -n` | `typeset -n` | `declare -n` | ✓ Implemented |
| Uppercase | `${var^^}` | `${(U)var}` | `${var^^}` | **TODO: zsh flags** |
| Lowercase | `${var,,}` | `${(L)var}` | `${var,,}` | **TODO: zsh flags** |
| First upper | `${var^}` | N/A | `${var^}` | ✓ Implemented |
| First lower | `${var,}` | N/A | `${var,}` | ✓ Implemented |
| Mapfile | `mapfile` | N/A | `mapfile` | ✓ Implemented |
| Readarray | `readarray` | N/A | `readarray` | ✓ Implemented |
| Glob qualifiers | N/A | `*(.)` `*(/)` | `*(.)` `*(/)` | ✓ Implemented |
| Extended glob | `?()/*()/+()` | `#/##/^` | Both | ✓ Bash style |
| Herestring | `<<<` | `<<<` | `<<<` | **BUG: Not working** |

### To Be Implemented (Zsh Parameter Flags)

| Flag | Syntax | Purpose | Priority |
|------|--------|---------|----------|
| `(U)` | `${(U)var}` | Uppercase | HIGH |
| `(L)` | `${(L)var}` | Lowercase | HIGH |
| `(C)` | `${(C)var}` | Capitalize words | MEDIUM |
| `(f)` | `${(f)var}` | Split on newlines | HIGH |
| `(j:X:)` | `${(j:,:)arr}` | Join with separator | HIGH |
| `(s:X:)` | `${(s:,:)var}` | Split on separator | HIGH |
| `(o)` | `${(o)arr}` | Sort ascending | MEDIUM |
| `(O)` | `${(O)arr}` | Sort descending | MEDIUM |
| `(k)` | `${(k)assoc}` | Associative array keys | MEDIUM |
| `(v)` | `${(v)assoc}` | Associative array values | MEDIUM |

---

## Compatibility Targets

### Realistic Goals

| Profile | Target | Notes |
|---------|--------|-------|
| `set -o lush` | N/A | This IS lush, no compatibility target |
| `set -o bash` | 95%+ | Common bash scripts run unmodified |
| `set -o zsh` | 90%+ | Common zsh scripts run unmodified |
| `set -o posix` | 99%+ | Strict POSIX sh compliance |

### What We Explicitly Do NOT Support

- Bash's `$"..."` locale quoting (niche feature)
- Zsh's anonymous functions in expansions (architectural limitation)
- Ksh93-specific syntax beyond POSIX
- Fish/Elvish/Nushell syntax
- Complete 100% emulation of any shell

---

## Implementation Priority

### Phase 1: Fix Known Bugs (BLOCKING)
All compatibility work is meaningless if core features are broken.

1. Array element assignment `arr[n]=value` (Issues #40, #44)
2. Associative array initialization and introspection (Issues #45, #46)
3. Brace expansion edge cases (Issue #42)
4. pushd/popd/dirs in script mode (Issue #43)
5. Herestring `<<<` not working (NEW - to be filed)

### Phase 2: Syntax Bridging - Builtins
1. Implement `shopt` builtin (bridges to setopt/unsetopt)
2. Verify `typeset` works as alias to `declare`
3. Add any missing bash/zsh builtin aliases

### Phase 3: Syntax Bridging - Parameter Expansion
1. Implement zsh parameter flag parser
2. `${(U)var}` → uppercase (shares code with `${var^^}`)
3. `${(L)var}` → lowercase (shares code with `${var,,}`)
4. `${(f)var}` → split on newlines
5. `${(j:X:)arr}` → join with separator
6. `${(s:X:)var}` → split on separator
7. `${(o)arr}` / `${(O)arr}` → sort

### Phase 4: Profile System Polish
1. Verify all profile defaults match target shells
2. Ensure array indexing switches correctly (0 vs 1 based)
3. Test profile loading and saving
4. Update documentation

### Phase 5: Comprehensive Testing
1. Expand test suite with real-world scripts
2. Document compatibility exceptions
3. Create migration guides for bash/zsh users

---

## Summary

Lush provides **pragmatic compatibility** through:

- **Shell Profiles**: Preset defaults matching bash/zsh/POSIX
- **Universal Syntax**: Both bash and zsh syntax work everywhere
- **User Freedom**: Any setting can be customized and persisted
- **Honest Limitations**: Clear documentation of what works and what doesn't

This approach:
- Preserves lush's identity as a modern, innovative shell
- Respects users coming from bash or zsh
- Maintains the unified config registry as a unique strength
- Sets realistic, achievable compatibility goals

---

**Document Status**: APPROVED - Ready for implementation planning
