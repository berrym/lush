# LLE Completion System

**Context-aware tab completion for the Lush Line Editor**

---

## Table of Contents

1. [Overview](#overview)
2. [Completion Types](#completion-types)
3. [Context-Aware Builtin Completions](#context-aware-builtin-completions)
4. [Menu Navigation](#menu-navigation)
5. [Completion Sources](#completion-sources)
6. [Configuration](#configuration)
7. [Technical Details](#technical-details)
8. [Troubleshooting](#troubleshooting)

---

## Overview

LLE's completion system is built into the line editor, not bolted on. It understands shell syntax and provides context-appropriate completions at every cursor position.

### Key Features

- **Context awareness**: Knows whether you're completing a command, argument, option, or variable
- **45 builtin completions**: Every completable builtin has specific completion logic
- **Real-time**: Completions update as you type
- **Menu interface**: Navigate multiple options with keyboard
- **Integration**: Works with history, multi-line editing, and syntax highlighting

### Quick Start

```bash
# Command completion
gi<Tab>              # -> git

# File completion
cat /etc/pa<Tab>     # -> cat /etc/passwd

# Option completion  
ls --co<Tab>         # -> ls --color

# Variable completion
echo $HO<Tab>        # -> echo $HOME

# Builtin-specific
set -o <Tab>         # Shows all shell options
```

---

## Completion Types

### Command Completion

Commands are completed from:
- Shell builtins
- Functions
- Aliases
- Executables in `$PATH`

```bash
$ ec<Tab>            # echo, env, etc.
$ git ch<Tab>        # checkout (if git completions available)
```

Commands are validated against the system. Valid commands appear in green, invalid in red.

### File Completion

Files and directories are completed based on the current word:

```bash
$ cat /etc/pa<Tab>   # passwd, pam.d/, etc.
$ cd ~/Doc<Tab>      # Documents/
$ vim src/**/*.c<Tab>  # Recursive glob completion
```

Features:
- Relative and absolute paths
- Home directory expansion (`~`)
- Special character handling
- Directory indicator (`/`)

### Variable Completion

After `$`, complete variable names:

```bash
$ echo $HO<Tab>      # $HOME
$ echo ${PA<Tab>     # ${PATH}
$ echo $LUSH_<Tab> # All LUSH_* variables
```

Includes:
- Environment variables
- Shell variables
- Special variables (`$?`, `$#`, `$$`, etc.)

### Option Completion

Command options are completed based on context:

```bash
$ ls -<Tab>          # -a -l -h ...
$ ls --<Tab>         # --all --long --help ...
$ grep --col<Tab>    # --color
```

### Hostname Completion

After `@` or in SSH contexts:

```bash
$ ssh user@<Tab>     # Hosts from ~/.ssh/known_hosts, /etc/hosts
```

---

## Context-Aware Builtin Completions

All 45 completable shell builtins have context-specific completion logic. The completion system knows what arguments each builtin accepts.

### Shell Options (set, shopt)

```bash
$ set -o <Tab>
errexit  hashall  monitor  nounset  pipefail  posix  verbose  xtrace ...

$ set +o err<Tab>    # -> set +o errexit
$ shopt -s ext<Tab>  # -> shopt -s extglob
```

### Config System (config)

```bash
$ config <Tab>
get  set  show  save  reset

$ config show <Tab>
shell  completion  display  history  keybindings

$ config set shell.<Tab>
errexit  nounset  xtrace  pipefail  emacs  vi ...

$ config get <Tab>
# All config keys
```

### Debug Commands (debug)

```bash
$ debug <Tab>
on  off  vars  print  trace  profile  functions  help

$ debug on <Tab>     # 1 2 3 (debug levels)
$ debug print <Tab>  # Variable names
$ debug profile <Tab>
on  off  report
```

### Display System (display)

```bash
$ display <Tab>
lle  features  themes  status  stats  config  diagnostics  help

$ display lle <Tab>
diagnostics  status  info
```

### Theme Commands (theme)

```bash
$ theme <Tab>
list  set  show  info  colors  symbols

$ theme set <Tab>
default  corporate  dark  colorful  minimal  classic

$ theme symbols <Tab>
unicode  ascii  auto
```

### Directory Commands (cd, pushd)

```bash
$ cd <Tab>           # Directories only
$ cd ~/D<Tab>        # ~/Documents/, ~/Downloads/, etc.
$ pushd /var/<Tab>   # /var/log/, /var/lib/, etc.
```

### Export/Declare

```bash
$ export <Tab>       # Existing variables
$ export PA<Tab>     # PATH, PATH_BACKUP, etc.
$ declare -<Tab>     # -a -A -i -r -x ...
$ declare -A <Tab>   # Valid variable names
```

### Read

```bash
$ read -<Tab>        # -r -p -t -n -s -a ...
$ read -p "prompt" <Tab>  # Variable names
```

### Type/Which/Command

```bash
$ type <Tab>         # Commands, builtins, functions
$ which <Tab>        # External commands
$ command -v <Tab>   # All commands
```

### Alias/Unalias

```bash
$ alias <Tab>        # Defined aliases or new alias names
$ unalias <Tab>      # Only defined aliases
```

### Complete Builtin List

Builtins with context-aware completions:

| Builtin | Completes |
|---------|-----------|
| `alias` | Alias names |
| `bg` | Job numbers |
| `bind` | Keybinding actions |
| `break` | Loop levels |
| `builtin` | Builtin names |
| `cd` | Directories |
| `command` | Commands |
| `complete` | Completion specs |
| `config` | Config keys/values |
| `continue` | Loop levels |
| `debug` | Debug subcommands |
| `declare` | Options, variables |
| `dirs` | Stack positions |
| `disown` | Job numbers |
| `display` | Display subcommands |
| `enable` | Builtins |
| `eval` | Commands |
| `exec` | Commands, files |
| `export` | Variables |
| `fc` | History commands |
| `fg` | Job numbers |
| `getopts` | Option strings |
| `hash` | Commands |
| `help` | Builtins |
| `history` | History options |
| `jobs` | Job options |
| `kill` | Signals, PIDs |
| `local` | Variables |
| `popd` | Stack positions |
| `pushd` | Directories |
| `pwd` | Options |
| `read` | Options, variables |
| `readonly` | Variables |
| `return` | Exit codes |
| `set` | Options |
| `shift` | Count |
| `shopt` | Shell options |
| `source` | Files |
| `test` | Expressions |
| `theme` | Theme subcommands |
| `trap` | Signals |
| `type` | Commands |
| `typeset` | Options, variables |
| `ulimit` | Resource options |
| `umask` | Modes |
| `unalias` | Defined aliases |
| `unset` | Variables, functions |
| `wait` | PIDs, job numbers |

---

## Menu Navigation

When multiple completions are available, LLE displays a menu.

### Display Modes

| Count | Display |
|-------|---------|
| 1 | Auto-complete immediately |
| 2-6 | Horizontal inline list |
| 7-12 | Vertical list |
| 13+ | Scrollable menu with position indicator |

### Navigation Keys

| Key | Action |
|-----|--------|
| `Tab` | Next item / Accept if no menu |
| `Shift-Tab` | Previous item |
| `Up` / `Ctrl-P` | Move up |
| `Down` / `Ctrl-N` | Move down |
| `Left` / `Ctrl-B` | Move left (multi-column) |
| `Right` / `Ctrl-F` | Move right (multi-column) |
| `Enter` | Accept selection, execute |
| `Space` | Accept selection, continue editing |
| `Escape` | Cancel completion |

### Menu Display

```bash
$ git ch<Tab>
  checkout    cherry      cherry-pick
> cherry-pick
```

The selected item updates the command line in real-time.

### External Command Selection

When an external command shadows a builtin (e.g., `/usr/bin/echo` vs the `echo` builtin), selecting the external command inserts its full path:

```bash
$ echo<Tab>
  echo (builtin)
> echo (/usr/bin/echo)
# Selecting the external version inserts: /usr/bin/echo
```

---

## Completion Sources

LLE uses multiple sources for completions.

### Built-in Sources

| Source | Provides |
|--------|----------|
| Commands | PATH executables, builtins, functions, aliases |
| Files | Filesystem paths |
| Variables | Environment and shell variables |
| History | Previous commands |
| Hostnames | SSH known hosts, /etc/hosts |

### Source Priority

When completing, sources are queried in order:
1. Context-specific completions (builtin arguments)
2. Command completions (at command position)
3. File completions (for arguments)
4. Variable completions (after `$`)

### SSH Hosts

SSH host completion reads from:
- `~/.ssh/known_hosts`
- `~/.ssh/config`
- `/etc/hosts`
- `/etc/ssh/ssh_known_hosts`

```bash
$ ssh user@<Tab>
server1.example.com  server2.example.com  localhost
```

---

## Configuration

### Enabling/Disabling

```bash
# Toggle completion
config set completion.enabled true
config set completion.enabled false

# Check status
config get completion.enabled
```

### Case Sensitivity

```bash
# Case-insensitive matching
config set completion.case_sensitive false

# Case-sensitive (default)
config set completion.case_sensitive true
```

### Menu Behavior

```bash
# Show menu on first tab
config set completion.menu_complete true

# Require second tab for menu
config set completion.menu_complete false
```

### Display Options

```bash
# Show completion type indicators
config set completion.show_types true

# Show descriptions (when available)
config set completion.show_descriptions true

# Maximum menu height
config set completion.max_menu_height 10
```

---

## Technical Details

### Context Analysis

LLE analyzes the command line to determine context:

```c
typedef enum {
    COMPLETION_CONTEXT_COMMAND,      // First word
    COMPLETION_CONTEXT_ARGUMENT,     // After command
    COMPLETION_CONTEXT_OPTION,       // After - or --
    COMPLETION_CONTEXT_VARIABLE,     // After $
    COMPLETION_CONTEXT_REDIRECT,     // After > < >>
    COMPLETION_CONTEXT_ASSIGNMENT,   // VAR=
    COMPLETION_CONTEXT_SUBCOMMAND,   // Builtin-specific
} completion_context_t;
```

### Word Detection

The current word is extracted with proper handling of:
- Quotes (single, double, ANSI-C)
- Escapes
- Variable expansion
- Globs

### Completion Pipeline

1. **Context analysis**: Determine what type of completion
2. **Word extraction**: Get the word being completed
3. **Source query**: Get candidates from relevant sources
4. **Filtering**: Match against current word
5. **Sorting**: Order by type, frequency, alphabetically
6. **Display**: Show menu or auto-complete

### Caching

Command existence checks are cached for performance:
- External commands: Checked once per session
- Builtins: Known statically
- Aliases/Functions: Updated on definition

```bash
# Clear command cache
# (Useful after PATH changes)
hash -r
```

### Memory Management

Completions use:
- Arena allocators for temporary data
- Reference counting for shared strings
- Pool allocation for menu items

---

## Troubleshooting

### No Completions Appear

**Check if completion is enabled:**
```bash
config get completion.enabled
```

**Check for errors:**
```bash
display lle diagnostics
```

**Verify PATH:**
```bash
echo $PATH
```

### Wrong Completions

**Force cache refresh:**
```bash
hash -r
```

**Check context:**
Type slowly and observe what completions appear. Context detection depends on proper parsing.

### Slow Completions

**Reduce completion sources:**
```bash
config set completion.complete_hostnames false
```

**Check PATH length:**
Very long PATH with many directories slows command completion.

**Disable path validation:**
```bash
config set display.validate_paths false
```

### Display Issues

**Terminal width:**
Resize terminal or check `$COLUMNS`.

**Color support:**
```bash
tput colors
```

**Unicode support:**
Ensure UTF-8 locale:
```bash
export LANG=en_US.UTF-8
```

### Debugging

**Enable debug output:**
```bash
debug on 2
# Trigger completion
debug off
```

**Check completion state:**
```bash
display lle diagnostics
```

---

## See Also

- [LLE_GUIDE.md](LLE_GUIDE.md) - Complete LLE documentation
- [BUILTIN_COMMANDS.md](BUILTIN_COMMANDS.md) - All shell builtins
- [CONFIGURATION.md](CONFIGURATION.md) - Configuration reference
