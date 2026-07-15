# Configuration

**The unified guide to lush's four configuration surfaces.**

---

## Table of Contents

1. [Overview: the four surfaces](#overview-the-four-surfaces)
2. [Decision tree: which surface should I use?](#decision-tree-which-surface-should-i-use)
3. [`mode` -- shell mode preset selector](#mode--shell-mode-preset-selector)
4. [`set` -- POSIX shell options](#set--posix-shell-options)
5. [`setopt` / `unsetopt` -- feature matrix](#setopt--unsetopt--feature-matrix)
6. [`shopt` -- bash-spelling sugar over `setopt`](#shopt--bash-spelling-sugar-over-setopt)
7. [`config` -- central registry](#config--central-registry)
8. [Configuration file](#configuration-file)
9. [Per-mode defaults](#per-mode-defaults)
10. [Startup ordering](#startup-ordering)

---

## Overview: the four surfaces

Lush configuration is layered across four orthogonal surfaces. Each surface
has one job and one canonical store. Aliases route through canonical state;
no surface has hidden state shared with another.

| Surface | What it touches | Polyglot reach |
|---|---|---|
| **`mode <name>`** | Identity selector. Re-seeds the three surfaces below for a chosen preset. | Lush-native; no analog in POSIX |
| **`set`** | POSIX shell options (`errexit`, `noclobber`, `vi`, `emacs`, `xtrace`, ...) | POSIX-strict + recognized bash extensions (`pipefail`) routed as aliases |
| **`setopt` / `unsetopt`** | Feature matrix (42 entries: `extended_glob`, `process_substitution`, `case_modification`, ...) | Lush canonical |
| **`shopt -s` / `-u`** | Same feature matrix as `setopt` | Bash-spelling sugar; identical underlying state |
| **`config`** | Central registry (`completion.fuzzy`, `display.syntax_highlighting`, `history.size`, ...) | Lush-native |

**`mode` is a meta-operation across the others.** Setting a mode re-seeds
the feature matrix, POSIX option defaults, and any registry options that
have per-mode default overrides. Picking a preset means asking for it: mid-
session mode changes overwrite tweaks to mode-aware options (re-seed-every-
time semantic). User config (`~/.config/lush/lushrc.toml`) layers on top of
mode presets at startup.

---

## Decision tree: which surface should I use?

```
I want to ...

  ... switch the shell's identity (lush, posix, bash, zsh)
      -> mode <name>

  ... toggle a POSIX-defined option (errexit, vi, emacs, noclobber, ...)
      -> set -o <name>   /  set +o <name>

  ... toggle a syntax/expansion feature (extended_glob, brace_expansion, ...)
      -> setopt <name>   /  unsetopt <name>      (lush-native canonical)
      -> shopt -s <name> /  shopt -u <name>      (bash-spelling sugar)

  ... change a configuration knob (completion behavior, display knobs, history
      sizing, theme settings, ...)
      -> config set <key> <value>
      -> or persist in ~/.config/lush/lushrc.toml
      -> or use the dedicated subsystem builtin: `display lle ...`,
         `history ...`, etc. (each is sugar over the registry)
```

If you're unsure whether something is a POSIX option, a feature-matrix
entry, or a registry knob: try `set -o`, `setopt`, and `config show` -- one
of them will list it.

---

## `mode` -- shell mode preset selector

Modes are mutually-exclusive identity presets. Lush has four:

| Mode | Purpose |
|---|---|
| `lush` | Curated lush-native default. Opinionated picks from bash and zsh, plus lush extensions. |
| `posix` | Strict POSIX sh compliance. |
| `bash` | Bash 5.x compatibility preset. |
| `zsh` | Zsh compatibility preset. |

### Setting the mode

```sh
mode               # print current mode + available modes
mode <name>        # switch to lush|posix|bash|zsh
mode --reset       # re-apply current preset (drops feature overrides)
mode --show        # verbose: current mode + state summary
```

```sh
$ mode
Current mode: lush

Available modes:
  lush   - Curated lush-native preset (default)
  posix  - Strict POSIX sh compliance
  bash   - Bash 5.x compatibility preset
  zsh    - Zsh compatibility preset

Usage: mode <lush|posix|bash|zsh>
       mode --reset    re-apply current preset
       mode --show     verbose status
```

### Setting the mode at startup

Three resolution mechanisms, in priority order:

1. **CLI flag** -- `lush --posix`, `lush --bash`, `lush --zsh`, `lush --lush`.
2. **Shebang** -- if running a script with `#!/bin/bash`, `#!/bin/zsh`, or
   `#!/bin/sh`, the corresponding mode is detected from the shebang line.
3. **Default** -- `lush` (curated).

The initial mode is resolved before user config loads, so `~/.config/lush/
lushrc.toml` settings layer on top of the right preset rather than being
overwritten by a later mode change.

### `set -o posix` / `set +o posix` (bash bridge)

For bash-script compatibility, `set -o posix` is recognized as a bash-bridge
alias that routes to `mode posix`. `set +o posix` symmetrically routes to
`mode lush` (lifts the POSIX preset). The canonical lush spelling is `mode
posix`; the `set -o posix` bridge exists so bash scripts using their
native idiom work unmodified.

`set -o {bash,zsh,lush}` are not recognized -- modes are not toggles. Use
`mode <name>`.

### Modes as presets, not restrictions

Lush modes are *initial-state selectors*, not capability locks. After
setting a mode, every feature, POSIX option, and config knob remains
adjustable. Lush is not bash and not zsh: it's its own shell that
recognizes their dialects and curates their defaults.

The pattern is:

```
mode bash             # apply bash-style defaults
setopt extended_glob  # but keep extended_glob enabled (it's a preference)
config set completion.fuzzy true  # and tune completion to taste
```

This is the polyglot promise made concrete: lush understands bash and
zsh spellings and curates their defaults, but the user is never locked in.

---

## `set` -- POSIX shell options

`set` is the POSIX-defined shell builtin. It takes single-letter and named
options that control POSIX behavior:

```sh
set -e               # exit on error (errexit)
set -o errexit       # same, named form
set -x               # trace execution (xtrace)
set -o vi            # vi editing mode
set -o emacs         # emacs editing mode (default)
set -o noclobber     # prevent overwriting with >
set -o pipefail      # pipeline fails if any stage fails
set -o ignoreeof     # don't exit on Ctrl-D
```

```sh
set -o               # list all options with current state
```

### Recognized bash extensions

`set` accepts bash extensions when they have a clear semantic meaning and a
canonical lush knob:

- `set -o pipefail` -- canonical (already a lush option; bash spelling
  matches).
- `set -o errtrace` / `set -E` -- ERR trap inherits into function bodies
  (and, in the longer plan, into subshells and command substitutions).
  Without it, the ERR trap fires only at the call site on the function's
  non-zero return, matching bash's default. (Issue #108.)
- `set -o functrace` / `set -T` -- DEBUG and RETURN traps inherit into
  function bodies. The DEBUG trap fires before each command in scope;
  the RETURN trap fires when a function returns. (Issue #109.)

These are wired via the same `option_map` in `src/posix_opts.c`. The
trap-inheritance gating itself lives in `src/signals.c` (`fire_err_trap`,
`fire_debug_trap`, `fire_return_trap`) and is consulted from the
executor walkers.

### What `set` does not do

`set` does not toggle feature-matrix entries (extended_glob, etc. -- use
`setopt`/`shopt`), and does not change configuration knobs (completion
behavior, display, history sizing, etc. -- use `config`).

---

## `setopt` / `unsetopt` -- feature matrix

`setopt` is the lush-canonical surface for the **feature matrix** -- the 42
entries in `src/shell_mode.c` covering syntax features (arrays, extended
test, process substitution), parameter expansions (case modification,
substring), globbing (extended glob, dot glob, globstar), and other
expansion-time behaviors.

```sh
setopt                       # list all features with current state
setopt extended_glob         # enable extended globbing
unsetopt dot_glob            # disable globs matching leading dots
setopt -p                    # print all features in re-usable format
setopt -q extended_glob; echo $?   # query silently (0=enabled, 1=disabled)
```

Feature names use lush-canonical spellings (`extended_glob`,
`process_substitution`, `case_modification`, ...). Underscores or no
underscores both work (`extended_glob` and `extendedglob` are equivalent).

`extended_glob` covers the structured operators that require a `(`: bash-style
`?(pat)`, `*(pat)`, `+(pat)`, `@(pat)`, `!(pat)` and zsh-style `(a|b)`
alternation. The zsh *bare* operators -- the `X#` / `X##` postfix quantifiers
and a leading `^` negation -- are gated separately by `zsh_extended_glob`
(alias `zshextglob`), which is **off in every mode**. Because those operators
turn ordinary punctuation into glob syntax, keeping them off makes a mid-word
`#` a literal word (`echo abc#def` -> `abc#def`), matching the bash and zsh
defaults (zsh's own `EXTENDED_GLOB` is off unless enabled). Turn them on with
`setopt zshextglob` to use the zsh quantifier and negation operators.

### Polyglot aliases

Many features have alternate spellings from bash or zsh; lush recognizes
them as aliases that route to the same underlying flag:

```sh
setopt arrays           # alias for indexed_arrays
setopt exttest          # alias for extended_test
setopt xpg_echo         # bash spelling
setopt bsd_echo         # zsh spelling (inverted: implies !xpg_echo)
setopt pushdminus       # alias for pushd_minus; off by default
```

`pushd_minus` (zsh `pushdminus`) is off in every profile. When set it
inverts the `pushd +N` / `pushd -N` (and `popd`) sign convention, so `+N`
counts from the right of the `dirs` list instead of the left.

`pushd_silent` (zsh `pushdsilent`), `pushd_to_home` (zsh `pushdtohome`), and
`pushd_ignore_dups` (zsh `pushdignoredups`) are likewise off in every profile
-- bash has no equivalents and zsh leaves them off, so the curated default
prints the stack, swaps the top two entries on a bare `pushd`, and keeps
duplicates. When set: `pushd_silent` suppresses the automatic directory-stack
print after `pushd`/`popd` (the explicit `dirs` command still prints);
`pushd_to_home` makes a bare `pushd` behave like `pushd $HOME`; and
`pushd_ignore_dups` drops an older copy of a directory from the stack when you
`pushd` back into it.

Inverted aliases (the `bsd_echo` row) are used when a knob has opposite-
named knobs across shells -- the alias system bridges them while keeping a
single canonical underlying flag.

---

## `shopt` -- bash-spelling sugar over `setopt`

`shopt` is the bash-style spelling for the same operations. `shopt -s X` is
identical to `setopt X`; `shopt -u X` is identical to `unsetopt X`. Both
operate on the same feature matrix:

```sh
shopt -s extended_glob       # same as: setopt extended_glob
shopt -u dot_glob            # same as: unsetopt dot_glob
shopt                        # list features (bash-style format)
shopt -p                     # print as re-usable shopt commands
shopt -o errexit             # bridge to set -o (POSIX option)
```

`shopt` is provided as polyglot sugar so bash scripts using the bash
spelling work unmodified. There is no functional difference from `setopt`;
internally both call the same `shell_feature_enable/disable` machinery.

---

## `config` -- central registry

`config` is the central per-knob configuration system (the registry; "creg"
internally). It holds hierarchical key-value pairs:

```sh
config show                  # show all sections
config show completion       # show one section
config set completion.fuzzy true
config get completion.fuzzy
config reload                # reload from ~/.config/lush/lushrc.toml
config save                  # save current state to lushrc.toml
config path                  # show config file paths
config reset-defaults        # write default lushrc.toml
```

### Sections

Current top-level sections:

| Section | Purpose |
|---|---|
| `shell` | Shell mode + POSIX option mirrors (read-only mirror; canonical is `mode`/`set`) |
| `history` | History sizing, file location, deduplication |
| `completion` | Tab completion behavior (fuzzy, case sensitivity, chain directories, ...) |
| `display` | Display layer toggles (syntax highlighting, autosuggestions, transient prompt, ...) |
| `behavior` | General shell behavior (auto_cd, spell correction, autocorrect tuning, ...) |
| `prompt` | Prompt theme, format, git integration |

### Subsystem-builtin sugar

Several subsystems expose dedicated builtins that are sugar over `config
set` for the same keys:

```sh
display lle autosuggestions on    # equivalent to: config set display.autosuggestions true
display lle completion chain_directories on
                                  # equivalent to: config set completion.chain_directories true
history ...                       # sugar over history.* keys
```

The dedicated builtins exist for discoverability (`display lle <TAB>`
guides users) and grouping. The registry is the single source of truth;
the builtins always sync.

### Extension surfaces: `display lle widget` / `hook` / `segment`

Three further `display lle` subcommands let scripts and configs
register first-class extension entities. These are not sugar over a
single config key -- they manage their own per-process registries --
but they share the registry's persistence convention (`config save`
serializes them where applicable):

```sh
display lle widget add upd-stat 'export LUSH_STAT=$(git rev-parse --short HEAD)'
display lle hook add post-command upd-stat
display lle segment add gitsha LUSH_STAT
# reference {gitsha} in your theme template to surface the value
```

- `display lle widget add NAME 'CMD'` registers a named LLE widget
  whose body is a shell command. Bindable from
  `~/.config/lush/keybindings.toml` exactly like a builtin widget.
- `display lle hook add HOOK WIDGET` attaches a widget to one of the
  ten LLE lifecycle hooks (`line-init`, `line-accepted`, `line-finish`,
  `buffer-modified`, `pre-command`, `post-command`, `completion-start`,
  `completion-end`, `history-search`, `terminal-resize`). The shell-
  side `lle_fire_pre_command` / `lle_fire_post_command` calls in the
  REPL are bridged to the widget-hooks-manager so the LLE hooks fire
  in lockstep with shell lifecycle events.
- `display lle segment add NAME VAR` registers a prompt segment whose
  visible content tracks `$VAR`. Reading `$VAR` on each render is
  O(1); a `display lle widget` + `display lle hook` pair updates the
  variable on a lifecycle event, the segment reflects the change at
  the next prompt redraw.

The trio composes by design: widget defines the work, hook decides
when, segment shows the result.

---

## Configuration file

The user configuration file is TOML format at:

```
~/.config/lush/lushrc.toml         # XDG-compliant primary location
~/.lushrc                          # legacy location (auto-migrated)
```

Run `config save` to write the current state to disk; `config reload` re-
reads and applies it. The file is loaded automatically at shell startup,
*after* mode resolution, so user config layers on top of mode presets.

### Example `lushrc.toml`

```toml
[shell]
mode = "lush"
errexit = false

[completion]
fuzzy = true
case_sensitive = false
chain_directories = true     # fish-style cascading on directory accept

[display]
syntax_highlighting = true
autosuggestions = true
transient_prompt = false

[history]
enabled = true
size = 10000
```

There is also a sibling shell-script file `~/.config/lush/lushrc` that's
sourced after the TOML file -- use this for things that need to run code
(aliases, functions, prompt customization).

---

## Per-mode defaults

Configuration options can declare per-mode default overrides. When `mode
<name>` is invoked, every option with a registered per-mode default for
that mode has its current value reset to the registered default.

### Example: `completion.chain_directories`

This option is the canonical demonstration of the per-mode-default
mechanism:

| Mode | `completion.chain_directories` default |
|---|---|
| `lush` | `true` (curated discoverability) |
| `bash` | `false` (matches bash convention) |
| `zsh` | `false` (matches zsh convention) |
| `posix` | `false` (matches posix sh convention) |

Switching modes mid-session re-seeds the option:

```sh
$ mode lush
$ display lle completion chain_directories
chain_directories: on

$ mode bash
$ display lle completion chain_directories
chain_directories: off
```

### Re-seed semantic

Mode change re-seeds *every time*, including mid-session. Picking a preset
means asking for it -- mid-session tweaks to mode-aware options are
overwritten by the new mode's defaults. This is intentional: it keeps
"mode" a clean preset operation rather than a soft hint.

If you want to preserve a value across mode changes, set it in your
`lushrc.toml` and run `config reload` after the mode switch.

### Options that don't have per-mode defaults

Most options are mode-invariant (one default that's right across all
modes). They're unaffected by mode changes. Only options whose right
default legitimately differs across shells register per-mode overrides.

---

## Restricted shell (`-r` / `--restricted` / `rlush`)

Lush supports a restricted-shell mode matching bash's `rbash`, zsh's
`RESTRICTED`, and POSIX 2024 `set -r`. It is invoked by:

- `lush -r` or `lush --restricted` on the command line
- `set -r` or `set -o restricted` from within a shell
- Invocation as `rlush` (symlink to the lush binary; bash's `rbash`
  pattern). The leading dash for login-shell invocation
  (`-rlush`) is recognized too.

### What's restricted

The bash-rbash set, applied verbatim:

1. `cd` is disabled.
2. `SHELL`, `PATH`, `HISTFILE`, `ENV`, `BASH_ENV` are made readonly.
3. Command names containing `/` are rejected (must resolve via `PATH`).
4. `.` / `source` with a filename containing `/` is rejected.
5. Output redirections `>`, `>|`, `>>`, `>&`, `&>`, `&>>`, `2>`, `2>>`
   are forbidden. Input redirection (`<`, `<<`, `<<<`) and FD-to-FD
   duplication (`2>&1`) are still allowed.
6. `exec` with arguments (process replacement) is disabled.
7. `set +r` and `set +o restricted` are rejected -- restricted mode
   is one-way once engaged.

### When restrictions engage

Restrictions activate **after** rc-file processing -- the same order
bash and zsh use. The intent is that an administrator can configure
the environment in `/etc/profile`, `~/.profile`, `~/.lush_login`, and
`~/.lushrc` (set up a locked-down `PATH`, define aliases, etc.) and
then have restrictions take effect before the user gets the prompt.
The `restricted_mode_engaged` flag (in `shell_opts`) is the gate
every enforcement site consults.

### Not a security boundary

This is the explicit, deliberate matching of bash's stance:

> "It is not intended to be a completely secure environment for
>  running shell scripts where absolute security is required."

A restricted shell is a **usability boundary** -- it keeps an unsuspecting
user from `cd`'ing out of the menu-shell's working directory or
clobbering files by accident. It is **not** a sandbox against a
determined adversary. Common escapes (none of which lush patches):

- `vi` / `vim` / `nano` / `less` / `man` -- shell-out via `:!sh`, `!sh`,
  `:shell`, etc.
- `find . -exec sh {} \;`
- `awk 'BEGIN{system("sh")}'`
- Any script invoked by full path that runs `unset PATH; exec /bin/sh`
- `ssh user@host` -- escapes by hopping to an unrestricted shell

Lock these down by curating `PATH` to a directory of admin-vetted
binaries before engaging restrictions, the same as you would with
bash's rbash. The restriction list is the perimeter; what's inside
the perimeter is the admin's responsibility.

---

## Startup ordering

For reference and debugging, the configuration startup sequence is:

1. `init_posix_options()` -- `shell_opts` to defaults.
2. `parse_opts()` -- parse CLI arguments, including `--posix`/`--bash`/
   `--zsh`/`--lush` mode flags.
3. `detect_initial_mode()` -- resolve initial mode in priority:
   - CLI flag, if any.
   - Shebang of the script argument, if any.
   - Default to `lush`.
4. `apply_mode_preset(initial_mode)` -- set the active mode, drop any
   feature overrides, update legacy bookkeeping. Registry isn't
   initialized yet, so the registry-side write is deferred.
5. `config_init()`:
   - Register all sections with their declared defaults.
   - Apply per-mode default overrides for the active mode.
   - Load `~/.config/lush/lushrc.toml` -- user values layer on top.
6. `config_apply_settings()` -- propagate registry values to runtime
   state (legacy fields, symbol table, subsystem hooks).
7. **Login + interactive rc-file sourcing** (`config_execute_*` series).
8. **`restricted_mode_engage()` if `-r` was requested.** Lockdown of
   protected variables happens here, AFTER step 7, so admin rc files
   can configure the environment before restrictions bite.

The `mode` builtin and the `set -o posix`/`set +o posix` bridge use
`apply_mode_preset()` at runtime; the registry side of the re-seed fires
because the registry is now initialized.

---

## See also

- [PHILOSOPHY.md](PHILOSOPHY.md) -- founding principles (identity vs
  polyglot, surface separation, POSIX as baseline not restriction).
- [VISION.md](VISION.md) -- project philosophy and design ambitions.
- [USER_GUIDE.md](USER_GUIDE.md) -- complete feature reference.
- [EXTENDED_SYNTAX.md](EXTENDED_SYNTAX.md) -- syntax-bridging details.
- [SPEC-COMPATIBILITY.md](development/SPEC-COMPATIBILITY.md) --
  compatibility targets and verification.
