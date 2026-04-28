# Shell Modes

**Running Lush in different compatibility modes**

Lush can operate in four distinct modes, each providing different feature sets and behaviors. This allows you to run scripts from different shell environments and choose the right balance between features and portability.

---

## Table of Contents

1. [Overview](#overview)
2. [Lush Mode](#lush-mode)
3. [POSIX Mode](#posix-mode)
4. [Bash Mode](#bash-mode)
5. [Zsh Mode](#zsh-mode)
6. [Feature Matrix](#feature-matrix)
7. [Switching Modes](#switching-modes)
8. [Shebang Detection](#shebang-detection)
9. [Per-Feature Overrides](#per-feature-overrides)
10. [Best Practices](#best-practices)

---

## Overview

| Mode | Purpose | Feature Set |
|------|---------|-------------|
| **Lush** | Default interactive mode | Curated set — opinionated picks from Bash and Zsh, plus Lush extensions |
| **POSIX** | Maximum portability | POSIX sh standard only |
| **Bash** | Bash script compatibility | Bash 4.x features |
| **Zsh** | Zsh script compatibility | Zsh features |

> Modes are **presets, not restrictions**. Any feature can be enabled or disabled at runtime via `set -o <feature>` / `set +o <feature>` regardless of the active mode — the mode just sets initial defaults.

Modes affect:
- Available syntax (arrays, `[[]]`, etc.)
- Command behavior
- Option names and defaults
- Error handling

Modes do NOT affect:
- LLE (always available interactively)
- Integrated debugger
- Configuration system
- Theme system

---

## Lush Mode

**The default mode. Use this for interactive sessions and Lush-native scripts.**

Lush mode is a **curated, opinionated set of defaults** — *not* "everything from bash plus everything from zsh". It picks specific features from each, rejects features deemed niche or surprising, and adds a small number of Lush-specific capabilities. See `docs/VISION.md` for the design philosophy and `src/shell_mode.c` for the authoritative feature matrix.

### Enabling

```bash
set -o lush           # Enable
set +o lush           # Disable (switches to POSIX)
```

### Features included by default

Curated picks active in Lush mode (see `src/shell_mode.c` for the complete table):

**From Bash**
- Indexed arrays: `arr=(a b c)`
- Associative arrays: `declare -A map`
- Extended test: `[[ ]]`
- Process substitution: `<()`, `>()`
- Extended parameter expansion
- Arithmetic: `(())`, `let`
- `select` loop
- Case fall-through: `;&`, `;;&`
- `time` keyword

**From Zsh**
- Glob qualifiers: `*(.)`, `*(/)`, `*(@)`
- Hook functions: `precmd`, `preexec`, `chpwd`, `periodic`
- Hook arrays: `precmd_functions`, etc.
- Extended globbing patterns

**Lush-Specific**
- Integrated debugging: `debug on`
- Plugin system foundation
- Enhanced display system
- Configuration interface

### Deliberately not enabled by default

Lush mode rejects a small set of features that one or both of bash/zsh have on by default. Each rejection is a documented design choice (see `src/shell_mode.c` for the per-feature reasoning):

- `dot_glob` — globs do *not* match leading `.` files. Explicit is better than surprising.
- `share_history` — history is per-shell-session, not auto-shared across concurrent shells. Reduces confusing cross-session interference.
- `hist_verify` — history-substitution results are not pre-displayed for confirmation. Faster workflow.
- `auto_pushd` / `cdable_vars` — `cd` does not implicitly push or treat words as variable lookups. Simpler mental model.
- `locale_quoting` (bash's `$"..."`) — niche bash-only feature; not propagated.

These can be opted into at runtime with `set -o <feature>` if you prefer the bash or zsh default behavior — modes are presets, not restrictions.

### When to Use

- Interactive shell sessions
- Scripts that will only run in Lush
- Scripts that need debugging features
- Scripts that use hooks

### Example Script

```bash
#!/usr/bin/env lush

# Uses Lush-specific features
declare -A config
config[debug]=true
config[verbose]=false

precmd() {
    [[ ${config[debug]} == true ]] && debug vars
}

files=(*.txt)
for f in "${files[@]}"; do
    [[ -f $f ]] && process "$f"
done
```

---

## POSIX Mode

**Maximum portability. Scripts run on any POSIX-compliant shell.**

POSIX mode restricts Lush to the POSIX shell specification. Use this for scripts that must run on different systems and shells.

### Enabling

```bash
set -o posix            # Enable
set +o posix            # Disable
lush --posix script.sh    # Run script in POSIX mode
```

### Restrictions

POSIX mode disables:

| Feature | Alternative |
|---------|-------------|
| Arrays | Use positional parameters or string parsing |
| `[[` extended test | Use `[` (test) |
| Process substitution | Use temporary files or pipes |
| Extended globbing | Use standard globs |
| `(())` arithmetic command | Use `$(())` expansion |
| Here-strings `<<<` | Use here-documents `<<` |
| `select` loop | Write your own menu |
| Case fall-through | Structure cases differently |
| Glob qualifiers | Use `find` or test in loop |

### What Still Works

POSIX mode retains:
- Standard control flow: `if`, `for`, `while`, `case`, `until`
- Functions (POSIX syntax)
- Standard parameter expansion
- Pipes, redirections
- Command substitution `$()`
- Arithmetic expansion `$(())`
- All POSIX builtins
- Here-documents

### When to Use

- Scripts for multiple platforms
- System scripts that may run on minimal shells
- Scripts in `/etc` that use `/bin/sh`
- Maximum compatibility requirements

### Example Script

```bash
#!/usr/bin/env lush
set -o posix

# POSIX-compliant: no arrays, no [[]]
files=$(ls *.txt 2>/dev/null)
for f in $files; do
    if [ -f "$f" ]; then
        echo "Processing: $f"
    fi
done

# Arithmetic with $(()), not (())
count=0
while [ $count -lt 10 ]; do
    count=$((count + 1))
done
```

---

## Bash Mode

**Bash 4.x compatibility. Run existing Bash scripts.**

Bash mode provides compatibility with Bash scripts, enabling Bash-specific features while maintaining Bash semantics.

### Enabling

```bash
set -o bash             # Enable
set +o bash             # Disable
lush --bash script.sh     # Run script in Bash mode
```

### Features

Bash mode enables:

- Indexed arrays
- Associative arrays (`declare -A`)
- Extended test `[[]]`
- Process substitution `<()`, `>()`
- Arithmetic command `(())`
- Extended globbing (`shopt -s extglob`)
- `select` loop
- Case fall-through `;&`, `;;&`
- `time` keyword
- Nameref variables (`local -n`)
- Extended parameter expansion
- Here-strings `<<<`

### Bash-Specific Behaviors

- `shopt` for shell options
- `BASH_*` variables populated
- Bash-style word splitting
- Bash-style pathname expansion

### What's Not Included

Bash mode does NOT include:
- Zsh glob qualifiers
- Lush hook system
- Zsh-specific parameter expansions
- Plugin system

### When to Use

- Running existing Bash scripts
- Writing scripts for both Bash and Lush
- Teams migrating from Bash

### Example Script

```bash
#!/usr/bin/env lush
set -o bash

# Standard Bash script
declare -a files=()
declare -A counts

while IFS= read -r -d '' file; do
    files+=("$file")
done < <(find . -name "*.txt" -print0)

for file in "${files[@]}"; do
    ext="${file##*.}"
    ((counts[$ext]++))
done

for ext in "${!counts[@]}"; do
    echo "$ext: ${counts[$ext]}"
done
```

---

## Zsh Mode

**Zsh compatibility. Run existing Zsh scripts.**

Zsh mode enables Zsh-specific features and behaviors.

### Enabling

```bash
set -o zsh              # Enable
set +o zsh              # Disable
lush --zsh script.sh      # Run script in Zsh mode
```

### Features

Zsh mode includes everything in Bash mode, plus:

- Glob qualifiers: `*(.)`, `*(/)`, `*(@)`, `*(*)`
- Extended glob patterns
- Zsh-style parameter expansion
- Hook functions: `precmd`, `preexec`, `chpwd`
- Hook arrays
- Zsh option names

### Zsh-Specific Behaviors

- Zsh-style word splitting (different from Bash)
- Glob qualifiers always available
- Zsh option name compatibility

### What's Not Included

- Full Zsh module system
- Zsh completion system (uses LLE's)
- Some esoteric Zsh features

### When to Use

- Running existing Zsh scripts
- Scripts using glob qualifiers
- Scripts using Zsh hooks
- Teams migrating from Zsh

### Example Script

```bash
#!/usr/bin/env lush
set -o zsh

# Zsh-style script
# Only regular files, sorted by modification time
for file in *(.) ; do
    echo "Regular file: $file"
done

# Only directories
for dir in *(/) ; do
    echo "Directory: $dir"
done

# Hook function
precmd() {
    echo "About to show prompt"
}
```

---

## Feature Matrix

| Feature | POSIX | Bash | Zsh | Lush |
|---------|:-----:|:----:|:---:|:------:|
| **Syntax** |
| Basic shell syntax | Yes | Yes | Yes | Yes |
| Arrays (indexed) | - | Yes | Yes | Yes |
| Arrays (associative) | - | Yes | Yes | Yes |
| `[[ ]]` extended test | - | Yes | Yes | Yes |
| Process substitution | - | Yes | Yes | Yes |
| `(( ))` arithmetic | - | Yes | Yes | Yes |
| `let` builtin | - | Yes | Yes | Yes |
| Here-strings `<<<` | - | Yes | Yes | Yes |
| Case fall-through | - | Yes | Yes | Yes |
| `select` loop | - | Yes | Yes | Yes |
| `time` keyword | - | Yes | Yes | Yes |
| Extended globbing | - | Yes | Yes | Yes |
| Glob qualifiers | - | - | Yes | Yes |
| Nameref `local -n` | - | Yes | Yes | Yes |
| **Parameter Expansion** |
| POSIX forms | Yes | Yes | Yes | Yes |
| Case modification | - | Yes | Yes | Yes |
| Pattern substitution | - | Yes | Yes | Yes |
| Indirect `${!var}` | - | Yes | Yes | Yes |
| Transformations `${var@Q}` | - | Yes | Yes | Yes |
| **Functions** |
| Basic functions | Yes | Yes | Yes | Yes |
| `local` variables | Partial | Yes | Yes | Yes |
| Anonymous functions | - | - | Yes | Yes |
| **Hooks** |
| precmd | - | - | Yes | Yes |
| preexec | - | - | Yes | Yes |
| chpwd | - | - | Yes | Yes |
| periodic | - | - | - | Yes |
| Hook arrays | - | - | Yes | Yes |
| **Other** |
| LLE editor | Yes | Yes | Yes | Yes |
| Debugger | Yes | Yes | Yes | Yes |
| Config system | Yes | Yes | Yes | Yes |
| Plugin system | - | - | - | Yes |

---

## Switching Modes

### Interactive

```bash
# Check current mode
set -o | grep -E "posix|bash|zsh|lush"

# Switch modes
set -o lush
set -o posix
set -o bash
set -o zsh
```

### In Scripts

```bash
#!/usr/bin/env lush
set -o bash   # Script runs in Bash mode
```

### Command Line

```bash
lush --posix script.sh
lush --bash script.sh
lush --zsh script.sh
lush script.sh         # Lush mode (default)
```

### Config File

```bash
# In ~/.lushrc
set -o lush  # Default mode for interactive sessions
```

---

## Shebang Detection

Lush can detect the intended mode from script shebangs:

| Shebang | Detected Mode |
|---------|---------------|
| `#!/bin/sh` | POSIX |
| `#!/usr/bin/env sh` | POSIX |
| `#!/bin/bash` | Bash |
| `#!/usr/bin/env bash` | Bash |
| `#!/bin/zsh` | Zsh |
| `#!/usr/bin/env zsh` | Zsh |
| `#!/usr/bin/env lush` | Lush |
| `#!/usr/bin/env lush --posix` | POSIX |
| `#!/usr/bin/env lush --bash` | Bash |

When sourcing scripts or running without explicit mode, Lush examines the shebang and adjusts behavior accordingly.

---

## Per-Feature Overrides

You can enable or disable individual features regardless of mode:

```bash
# In POSIX mode, but enable arrays
set -o posix
shopt -s arrays         # Enable arrays only

# In Lush mode, but disable process substitution
set -o lush
shopt -u procsub        # Disable process substitution

# Check feature status
shopt                   # List all features
shopt arrays            # Check specific feature
```

### Available Feature Flags

```bash
shopt -s extglob        # Extended globbing
shopt -s arrays         # Indexed arrays
shopt -s assoc_arrays   # Associative arrays
shopt -s procsub        # Process substitution
shopt -s exttest        # [[ ]] extended test
shopt -s globqual       # Glob qualifiers
```

---

## Best Practices

### Choosing a Mode

1. **Interactive work**: Use Lush mode (default)
2. **New scripts for Lush**: Use Lush mode
3. **Scripts for multiple systems**: Use POSIX mode
4. **Existing Bash scripts**: Use Bash mode
5. **Existing Zsh scripts**: Use Zsh mode

### Script Portability

```bash
#!/usr/bin/env lush
# Explicitly set mode at script start
set -o posix  # or bash, zsh, lush

# Script continues in that mode
```

### Testing Across Modes

```bash
# Test script in different modes
lush --posix script.sh && echo "POSIX OK"
lush --bash script.sh && echo "Bash OK"
lush script.sh && echo "Lush OK"
```

### Gradual Migration

When migrating scripts:

1. Start in compatibility mode (bash or zsh)
2. Test thoroughly
3. Gradually adopt Lush features
4. Eventually switch to Lush mode if desired

### Documentation

Always document which mode your script requires:

```bash
#!/usr/bin/env lush
# requires: lush mode (uses hooks)
# or
#!/usr/bin/env lush --posix
# compatible: any POSIX shell
```

---

## See Also

- [USER_GUIDE.md](USER_GUIDE.md) - Complete feature reference
- [EXTENDED_SYNTAX.md](EXTENDED_SYNTAX.md) - Extended syntax details
- [SHELL_OPTIONS.md](SHELL_OPTIONS.md) - All shell options
- [FEATURE_COMPARISON.md](FEATURE_COMPARISON.md) - Comparison with other shells
