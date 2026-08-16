# Lush Syntax Bridging Architecture

**Document Version**: 1.0.0  
**Created**: 2026-01-13  
**Status**: Reference Documentation

---

## Executive Summary

Lush implements a unique **syntax bridging architecture** that fundamentally differs from how other shells handle compatibility. Rather than emulating other shells (which restricts features), lush provides a **unified feature engine** where multiple syntaxes map to the same underlying operations.

**Key Principle**: Syntax is an interface layer. Profiles are presets, not restrictions.

---

## The Problem with Traditional Shell Emulation

### How Other Shells Do It

**Zsh's `emulate bash`:**
```bash
emulate bash  # Restricts zsh to bash-compatible features only
```
- Disables zsh-specific features
- Limits what you can do
- You lose functionality to gain compatibility

**Bash's POSIX mode:**
```bash
set -o posix  # Restricts bash to POSIX-only features
```
- Same pattern: restriction-based compatibility

### The Limitation

Traditional emulation forces a choice:
- Use bash syntax -> lose zsh features
- Use zsh syntax -> lose bash compatibility
- Scripts written for one shell don't work in another

---

## The Lush Way: Syntax Bridging

### Core Concept

Lush treats syntax as an **interface layer** that maps to a **unified feature engine**:

```
User Input               Translation Layer           Feature Engine
---------------------------------------------------------------------
shopt -s extglob    ->    bin_shopt()           ->    FEATURE_EXTENDED_GLOB
setopt extended_glob ->   bin_setopt()          ->    FEATURE_EXTENDED_GLOB
                                v
                         Same result: extended glob enabled
```

Both commands enable the exact same feature. The syntax is just a preference.

### Parameter Expansion Example

```
Bash Syntax              Zsh Syntax              Result
-------------------------------------------------------
${var^^}            ==    ${(U)var}          ->   UPPERCASE
${var,,}            ==    ${(L)var}          ->   lowercase
${var^}             ==    ${(C)var}          ->   Capitalize
```

Both syntaxes are available in ALL profiles. A user can mix them:

```bash
# This works in lush regardless of profile:
name="hello"
echo "${name^^}"      # Bash syntax -> HELLO
echo "${(U)name}"     # Zsh syntax  -> HELLO
```

### Extended Glob Example

```
Bash Extglob            Zsh Extglob             Pattern
-------------------------------------------------------
*(pattern)         ==    pattern#           ->   zero or more
+(pattern)         ==    pattern##          ->   one or more
@(a|b)             ==    (a|b)              ->   alternation
!(pattern)         ==    ^pattern           ->   negation
```

Both syntaxes work. The underlying glob engine handles both.

---

## Shell Profiles: Presets, Not Restrictions

### What a Profile Does

A profile sets **default values** for the feature matrix:

```c
// SHELL_MODE_BASH defaults
[FEATURE_ARRAY_ZERO_INDEXED] = true,   // Arrays start at 0
[FEATURE_WORD_SPLIT]         = true,   // Word splitting on
[FEATURE_EXTENDED_GLOB]      = false,  // extglob off by default
[FEATURE_NULL_GLOB]          = false,  // nullglob off

// SHELL_MODE_ZSH defaults (lush's zsh PRESET, not byte-for-byte zsh)
[FEATURE_ARRAY_ZERO_INDEXED] = false,  // Arrays start at 1 (matches zsh)
[FEATURE_WORD_SPLIT]         = false,  // Word splitting off (matches zsh)
[FEATURE_EXTENDED_GLOB]      = true,   // lush curation; real zsh KSH_GLOB is off
[FEATURE_NULL_GLOB]          = true,   // lush curation; real zsh defaults NOMATCH
```

### User Customization

Users can override ANY default:

```bash
set -o bash              # Load bash profile (bash defaults)
setopt null_glob         # Override: enable nullglob (zsh-like)
setopt extended_glob     # Override: enable extglob
```

The profile is just a starting point. Users customize freely.

### Why This Matters

**Traditional approach** (zsh's `emulate bash`):
- "You're in bash mode, so you can't use zsh features"

**Lush approach**:
- "You loaded bash defaults, but all features are still available"
- "Want zsh-style parameter flags? Just use them."
- "Want to mix `shopt` and `setopt`? Go ahead."

---

## Architecture Implementation

### Feature Matrix System

Located in `src/shell_mode.c`:

```c
// Per-mode feature defaults
static const bool posix_feature_defaults[FEATURE_COUNT] = { ... };
static const bool bash_feature_defaults[FEATURE_COUNT] = { ... };
static const bool zsh_feature_defaults[FEATURE_COUNT] = { ... };
static const bool lush_feature_defaults[FEATURE_COUNT] = { ... };

// Runtime state with user overrides
typedef struct {
    shell_mode_t current_mode;
    bool feature_overrides[FEATURE_COUNT];
    bool feature_override_set[FEATURE_COUNT];
} shell_mode_state_t;
```

### Feature Resolution

Three-level lookup in `shell_mode_allows()`:

```c
bool shell_mode_allows(shell_feature_t feature) {
    // 1. Check user override first
    if (g_shell_mode_state.feature_override_set[feature]) {
        return g_shell_mode_state.feature_overrides[feature];
    }
    
    // 2. Fall back to current mode's default
    return get_mode_defaults()[feature];
}
```

### Syntax Recognition

The tokenizer and parser recognize both syntaxes:

```c
// Tokenizer: Recognize both bash and zsh extglob
case '(':
    // Check for bash-style: ?(, *(, +(, @(, !(
    if (strchr("?*+@!", prev_char)) { ... }
    // Check for zsh-style: (a|b)suffix
    if (has_pipe_inside && has_suffix) { ... }

// Executor: Handle both parameter flag syntaxes
if (strncmp(text, "${(", 3) == 0) {
    // Zsh-style: ${(U)var}
    parse_zsh_param_flags(text, ...);
} else if (has_case_modifier(text)) {
    // Bash-style: ${var^^}
    apply_case_modification(text, ...);
}
```

---

## Practical Benefits

### For Bash Users

```bash
# Your bash scripts work as-is
arr=(one two three)
echo "${arr[0]}"           # Works (0-indexed in bash profile)
echo "${var^^}"            # Works
shopt -s extglob           # Works
echo +(pattern)            # Works
```

### For Zsh Users

```zsh
# Your zsh syntax works too
arr=(one two three)
echo "${arr[1]}"           # Works (1-indexed in zsh profile)
echo "${(U)var}"           # Works
setopt extended_glob       # Works
echo pattern##             # Works
echo *(.)                  # Glob qualifiers work
```

### For Polyglot Scripts

```bash
#!/usr/bin/env lush
# Mix syntaxes freely - use what's clearest for each case

# Bash-style case modification (familiar to bash users)
name="hello"
echo "${name^^}"

# Zsh-style word splitting (cleaner for arrays)
words="${(f)$(cat file.txt)}"

# Bash-style extglob (familiar pattern)
rm !(important).txt

# Zsh-style glob qualifiers (powerful file filtering)
ls *(.)                    # Only regular files
```

---

## Feature Matrix Reference

| Feature | POSIX | Bash | Zsh | Lush | Notes |
|---------|:-----:|:----:|:---:|:------:|-------|
| `indexed_arrays` | OFF | ON | ON | ON | `arr=(a b c)` |
| `associative_arrays` | OFF | ON | ON | ON | `declare -A` |
| `array_zero_indexed` | N/A | ON | OFF | ON | Bash=0, Zsh=1 |
| `extended_glob` | OFF | OFF | ON | ON | extglob patterns |
| `null_glob` | OFF | OFF | ON | ON | Unmatched -> nothing |
| `glob_qualifiers` | OFF | OFF | ON | ON | `*(.)` `*(/)` |
| `word_split` | ON | ON | OFF | OFF | IFS splitting |
| `case_modification` | OFF | ON | ON | ON | `${var^^}` |
| `zsh_param_flags` | OFF | OFF | ON | ON | `${(U)var}` |
| `process_substitution` | OFF | ON | ON | ON | `<(cmd)` |

---

## Design Rationale

### Why Not Emulation?

1. **Emulation is subtractive**: You lose features
2. **Emulation is incomplete**: Edge cases always differ
3. **Emulation limits learning**: Users stay in their comfort zone

### Why Syntax Bridging?

1. **Additive**: All features always available
2. **Educational**: Users discover equivalent syntax
3. **Practical**: Scripts from either shell work
4. **Future-proof**: New syntax can map to existing features

### The Lush Philosophy

> "Don't restrict users to one shell's way of thinking. Give them all the tools and let them choose the syntax that's clearest for each task."

---

## Testing and Verification

The syntax bridging is validated by the compatibility test suite:

```bash
# Both modes should pass 100%
./tests/bash_zsh_compat_test.sh ./build/lush --mode=bash  # 100%
./tests/bash_zsh_compat_test.sh ./build/lush --mode=zsh   # 100%

# Default mode works with both syntaxes
./tests/bash_zsh_compat_test.sh ./build/lush              # 100%
```

---

## Conclusion

Lush's syntax bridging architecture represents a departure from traditional shell design:

| Traditional Shells | Lush |
|-------------------|--------|
| Modes restrict features | Profiles set defaults |
| Pick one syntax | All syntaxes available |
| Emulation is approximate | Feature engine is unified |
| Scripts are shell-specific | Polyglot scripting enabled |

**The result**: A shell where bash users feel at home, zsh users feel at home, and both can learn from each other's syntax without switching shells.

---

## References

- `src/shell_mode.c` - Feature matrix implementation
- `src/executor.c` - Syntax handling (parameter expansion, globs)
- `src/tokenizer.c` - Syntax recognition
- `include/shell_mode.h` - Feature definitions
- `tests/bash_zsh_compat_test.sh` - Compatibility verification


