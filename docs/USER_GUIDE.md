# Lush User Guide

**Version**: 1.5.0  
**Complete feature reference for the Lush shell**

---

## Table of Contents

1. [Overview](#overview)
2. [Shell Modes](#shell-modes)
3. [Line Editing (LLE)](#line-editing-lle)
4. [Extended Syntax](#extended-syntax)
5. [Completion System](#completion-system)
6. [Hook System](#hook-system)
7. [Debugging](#debugging)
8. [Theme System](#theme-system)
9. [Configuration](#configuration)
10. [Builtin Commands](#builtin-commands)
11. [POSIX Compliance](#posix-compliance)
12. [Scripting](#scripting)

---

## Overview

Lush v1.5.0 is an advanced interactive shell with three distinguishing capabilities:

1. **LLE (Lush Line Editor)**: A native line editor with Emacs keybindings, context-aware completions, and syntax highlighting - not a readline wrapper
2. **Multi-Mode Architecture**: Run in POSIX, Bash, Zsh, or Lush mode depending on your needs
3. **Integrated Debugging**: Set breakpoints, step through code, inspect variables, and profile performance from within the shell

### Starting Lush

```bash
# Interactive shell
lush

# Execute a command
lush -c "echo 'Hello'"

# Run a script
lush script.sh

# Specific mode
lush --posix script.sh
lush --bash script.sh
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `-c COMMAND` | Execute command string and exit |
| `-i` | Force interactive mode |
| `-l` | Act as login shell |
| `-s` | Read commands from standard input |
| `-e` | Exit on error (`set -e`) |
| `-x` | Trace execution (`set -x`) |
| `-u` | Error on unset variables (`set -u`) |
| `-v` | Verbose mode (`set -v`) |
| `-f` | Disable pathname expansion (`set -f`) |
| `--posix` | POSIX compliance mode |
| `--bash` | Bash compatibility mode |
| `--zsh` | Zsh compatibility mode |
| `--help` | Show help message |
| `--version` | Show version information |

---

## Shell Modes

Lush operates in one of four modes, each with different feature sets and behaviors.

### Lush Mode (Default)

The recommended mode for interactive use and Lush-specific scripts. All features enabled:

- Extended syntax (arrays, `[[]]`, process substitution)
- Bash and Zsh compatible features
- Hook system (precmd, preexec, chpwd, periodic)
- Full LLE capabilities
- Integrated debugging

```bash
set -o lush    # Enable (usually default)
```

### POSIX Mode

Strict POSIX sh compliance for maximum portability:

- No arrays
- No `[[]]` (use `[ ]`)
- No process substitution
- No extended globbing
- POSIX-defined behavior only

```bash
set -o posix     # Enable POSIX mode
lush --posix   # Start in POSIX mode
```

### Bash Mode

Bash-compatible feature set:

- Indexed arrays: `arr=(a b c)`
- Associative arrays: `declare -A`
- Extended test: `[[]]`
- Process substitution: `<()`, `>()`
- Extended parameter expansion
- Bash-style brace expansion

```bash
set -o bash      # Enable Bash mode
```

### Zsh Mode

Zsh-compatible feature set:

- All Bash features
- Glob qualifiers: `*(.)`, `*(/)`, `*(@)`
- Extended parameter expansion forms
- Zsh-style option names

```bash
set -o zsh       # Enable Zsh mode
```

### Mode Feature Matrix

| Feature | POSIX | Bash | Zsh | Lush |
|---------|-------|------|-----|--------|
| Basic syntax | Yes | Yes | Yes | Yes |
| Indexed arrays | No | Yes | Yes | Yes |
| Associative arrays | No | Yes | Yes | Yes |
| `[[]]` extended test | No | Yes | Yes | Yes |
| Process substitution | No | Yes | Yes | Yes |
| Extended globbing | No | Yes | Yes | Yes |
| Glob qualifiers | No | No | Yes | Yes |
| Hook functions | No | No | Partial | Yes |
| Plugin system | No | No | No | Yes |

---

## Line Editing (LLE)

LLE is Lush's native line editor. It is not based on GNU Readline or any external library.

### Emacs Mode

LLE provides complete Emacs-style editing:

**Movement**

| Keybinding | Action |
|------------|--------|
| `Ctrl-A` | Beginning of line |
| `Ctrl-E` | End of line |
| `Ctrl-B` | Backward one character |
| `Ctrl-F` | Forward one character |
| `Alt-B` | Backward one word |
| `Alt-F` | Forward one word |

**Editing**

| Keybinding | Action |
|------------|--------|
| `Ctrl-D` | Delete character (or EOF on empty line) |
| `Backspace` | Delete backward |
| `Ctrl-K` | Kill to end of line |
| `Ctrl-U` | Kill to beginning of line |
| `Ctrl-W` | Kill previous word |
| `Alt-D` | Kill next word |
| `Ctrl-Y` | Yank (paste) killed text |
| `Alt-Y` | Yank-pop (cycle through kill ring) |
| `Ctrl-T` | Transpose characters |
| `Alt-T` | Transpose words |
| `Ctrl-_` | Undo |

**History**

| Keybinding | Action |
|------------|--------|
| `Ctrl-P` / `Up` | Previous history entry |
| `Ctrl-N` / `Down` | Next history entry |
| `Ctrl-R` | Reverse incremental search |
| `Alt-<` | Beginning of history |
| `Alt->` | End of history |

**Completion**

| Keybinding | Action |
|------------|--------|
| `Tab` | Complete |
| `Alt-?` | List completions |
| `Alt-*` | Insert all completions |

**Control**

| Keybinding | Action |
|------------|--------|
| `Ctrl-L` | Clear screen |
| `Ctrl-C` | Interrupt (SIGINT) |
| `Ctrl-Z` | Suspend (SIGTSTP) |
| `Ctrl-D` | EOF (on empty line) |

### Vi Mode

Vi mode is in development. Framework exists, targeting v1.4.0 release.

### Syntax Highlighting

LLE highlights your input in real-time:

- **Commands**: Valid commands highlighted, unknown commands indicated
- **Builtins**: Distinguished from external commands
- **Strings**: Single and double quotes
- **Variables**: `$var`, `${var}`, `$(...)`
- **Operators**: Pipes, redirections, logic operators
- **Comments**: `# comment`
- **Paths**: Path-shaped arguments are classified by *shape* (absolute
  `/...`, relative `./...`, home `~/...`) and *kind* (regular file vs.
  directory vs. missing). Defaults: directories underlined green,
  missing paths red, regular files unhighlighted (let the line itself
  speak). Themes can colour every shape × kind slot independently or
  override the kind-only `path_file` / `path_dir` fallbacks. See the
  `[syntax]` section in `examples/theme.toml`.
- **Errors**: Syntax errors indicated before execution

### Multi-Line Editing

Incomplete commands continue naturally with proper indentation:

```bash
$ for i in 1 2 3; do
>     echo "Number: $i"
> done
```

---

## Extended Syntax

Lush mode (and Bash/Zsh modes) support extended shell syntax beyond POSIX.

### Arrays

**Indexed Arrays**

```bash
# Declaration
fruits=(apple banana cherry)
declare -a numbers

# Assignment
fruits[0]="apple"
fruits[3]="date"

# Access
echo "${fruits[0]}"        # First element
echo "${fruits[@]}"        # All elements
echo "${#fruits[@]}"       # Count
echo "${!fruits[@]}"       # Indices

# Slicing
echo "${fruits[@]:1:2}"    # Elements 1-2
```

**Associative Arrays**

```bash
# Declaration (required)
declare -A colors

# Assignment
colors[apple]="red"
colors[banana]="yellow"
colors[cherry]="red"

# Access
echo "${colors[apple]}"    # red
echo "${colors[@]}"        # All values
echo "${!colors[@]}"       # All keys
```

### Arithmetic

**Arithmetic Expansion**

```bash
result=$((5 + 3))
result=$((count * 2))
result=$((a > b ? a : b))
```

**Arithmetic Command**

```bash
(( count++ ))
(( total += value ))
if (( count > 10 )); then
    echo "Over ten"
fi
```

**let Builtin**

```bash
let count=count+1
let "total = a + b"
```

**Operators**

| Operator | Description |
|----------|-------------|
| `+ - * /` | Basic arithmetic |
| `%` | Modulo |
| `**` | Exponentiation |
| `++ --` | Increment/decrement |
| `<< >>` | Bit shift |
| `& \| ^` | Bitwise AND, OR, XOR |
| `~` | Bitwise NOT |
| `< > <= >=` | Comparison |
| `== !=` | Equality |
| `&&` | Logical AND |
| `\|\|` | Logical OR |
| `? :` | Ternary |

### Extended Test

The `[[]]` construct provides enhanced testing:

```bash
# Pattern matching
[[ $file == *.sh ]]

# Regex matching
[[ $email =~ ^[a-z]+@[a-z]+\.[a-z]+$ ]]

# Logical operators
[[ -f $file && -r $file ]]
[[ $a == $b || $a == $c ]]

# Negation
[[ ! -d $dir ]]

# String comparison
[[ $str1 < $str2 ]]    # Lexicographic
```

### Process Substitution

Treat command output as a file:

```bash
# Compare outputs
diff <(ls dir1) <(ls dir2)

# Feed to command expecting file
wc -l <(find . -name "*.c")

# Write to process
tee >(gzip > file.gz) >(wc -l > count.txt)
```

### Parameter Expansion

**Case Modification**

```bash
name="hello world"
echo "${name^}"         # Hello world (first char upper)
echo "${name^^}"        # HELLO WORLD (all upper)
echo "${name,}"         # hello world (first char lower)
echo "${name,,}"        # hello world (all lower)
```

**Substitution**

```bash
path="/home/user/file.txt"
echo "${path/user/admin}"      # Replace first
echo "${path//\//\\}"          # Replace all
echo "${path/#\/home/~}"       # Replace at start
echo "${path/%.txt/.md}"       # Replace at end
```

**Substring**

```bash
str="Hello World"
echo "${str:0:5}"       # Hello
echo "${str:6}"         # World
echo "${str: -5}"       # World (note space)
echo "${str:0:-6}"      # Hello
```

**Length and Removal**

```bash
echo "${#str}"          # 11 (length)
echo "${path#*/}"       # home/user/file.txt (remove shortest prefix)
echo "${path##*/}"      # file.txt (remove longest prefix)
echo "${path%/*}"       # /home/user (remove shortest suffix)
echo "${path%%/*}"      # (empty, remove longest suffix)
```

**Indirect Expansion**

```bash
name="PATH"
echo "${!name}"         # Value of $PATH
```

**Transformation**

```bash
str="hello"
echo "${str@Q}"         # Quoted: 'hello'
echo "${str@E}"         # Escape sequences expanded
echo "${str@P}"         # Prompt expansion
echo "${str@A}"         # Assignment form: str='hello'
echo "${str@a}"         # Attributes (for declared vars)
```

### Extended Globbing

Enable with `shopt -s extglob`:

```bash
# Zero or one match
ls ?(pattern)

# Zero or more matches
ls *(pattern)

# One or more matches
ls +(pattern)

# Exactly one (negated)
ls @(pattern)

# Anything except
ls !(pattern)

# Examples
ls *.+(c|h)             # .c or .h files
ls !(*.o|*.a)           # Not .o or .a files
```

### Glob Qualifiers (Zsh/Lush Mode)

Filter glob results by file attributes:

```bash
ls *(.)    # Regular files only
ls *(/)    # Directories only
ls *(@)    # Symbolic links only
ls *(*)    # Executable files only
```

### Case Fall-Through

Extended case statement patterns:

```bash
case "$opt" in
    -v|--verbose)
        verbose=1
        ;&              # Fall through to next
    -d|--debug)
        debug=1
        ;;
    -*)
        ;;&             # Continue pattern testing
    *)
        echo "Unknown"
        ;;
esac
```

### Select Loop

Interactive menu:

```bash
select choice in "Option 1" "Option 2" "Quit"; do
    case $choice in
        "Option 1") echo "First" ;;
        "Option 2") echo "Second" ;;
        "Quit") break ;;
    esac
done
```

### Time Keyword

Measure execution time:

```bash
time sleep 1
time { cmd1; cmd2; cmd3; }
```

---

## Completion System

LLE provides context-aware completions.

### Completion Types

- **Command completion**: Executables in `$PATH`
- **Builtin completion**: All 50 shell builtins
- **File completion**: Paths and filenames
- **Variable completion**: `$VAR` names
- **Option completion**: Command-specific options

### Context-Aware Builtin Completions

All 45 completable builtins understand their arguments:

```bash
set -o <Tab>        # Shows all shell options
config set <Tab>    # Shows config sections
debug <Tab>         # Shows: on off vars print trace profile ...
display <Tab>       # Shows: lle features themes status
theme <Tab>         # Shows: list set show info
```

### Completion Behavior

```bash
command<Tab>        # Complete command name
/path/to/<Tab>      # Complete path
$VAR<Tab>           # Complete variable name
cmd --<Tab>         # Complete long options
cmd -<Tab>          # Complete short options
```

---

## Hook System

Lush provides Zsh-style hook functions.

### Hook Functions

**precmd**

Runs after each command completes, before the prompt is displayed:

```bash
precmd() {
    # Update terminal title
    echo -ne "\033]0;$(pwd)\007"
}
```

**preexec**

Runs after a command is entered but before it executes:

```bash
preexec() {
    # Log command
    echo "$(date): $1" >> ~/.command_log
}
```

**chpwd**

Runs when the current directory changes:

```bash
chpwd() {
    # Auto-ls on directory change
    ls
}
```

**periodic**

Runs periodically (interval set by `PERIOD` variable):

```bash
PERIOD=60    # Every 60 seconds
periodic() {
    # Check for new mail
    check_mail
}
```

### Hook Arrays

For multiple handlers on the same event:

```bash
# Add functions to arrays
precmd_functions+=(my_precmd_handler)
preexec_functions+=(my_preexec_handler)
chpwd_functions+=(my_chpwd_handler)
periodic_functions+=(my_periodic_handler)

# Functions are called in order
my_precmd_handler() {
    echo "First handler"
}

another_precmd_handler() {
    echo "Second handler"
}

precmd_functions=(my_precmd_handler another_precmd_handler)
```

---

## Debugging

Lush is the only shell with integrated interactive debugging.

### Enabling Debug Mode

```bash
debug on        # Basic debugging
debug on 2      # Detailed tracing
debug on 3      # Maximum verbosity
debug off       # Disable
```

### Inspecting State

```bash
debug vars              # Show all variables
debug print VAR         # Show specific variable
debug functions         # List defined functions
```

### Execution Tracing

```bash
debug trace on          # Enable tracing
# Commands show execution details
debug trace off         # Disable tracing
```

### Performance Profiling

```bash
debug profile on
# Run commands to profile
debug profile report    # Show timing information
debug profile off
```

### In Scripts

```bash
#!/usr/bin/env lush

# Debug specific section
debug on 2
complex_function
debug off

# Profile critical path
debug profile on
expensive_operation
debug profile report
debug profile off
```

For complete debugging documentation, see [DEBUGGER_GUIDE.md](DEBUGGER_GUIDE.md).

---

## Theme System

Lush includes a professional theme system.

### Available Themes

- **default**: Clean, minimalist
- **corporate**: Professional business
- **dark**: High contrast dark
- **colorful**: Vibrant development
- **minimal**: Ultra-clean
- **classic**: Traditional shell

### Theme Commands

```bash
theme list              # List all themes
theme show              # Current theme details
theme set <name>        # Switch themes
theme info <name>       # Theme information
theme colors            # Show color palette
```

### Git Integration

Prompts show git status when in a repository:

```bash
[user@host] ~/project (main) $           # Current branch
[user@host] ~/project (main +) $         # Uncommitted changes
[user@host] ~/project (main !) $         # Staged changes
```

### Symbol Modes

```bash
theme symbols unicode   # Unicode symbols
theme symbols ascii     # ASCII fallback
theme symbols auto      # Auto-detect (default)
```

---

## Configuration

### Config System

Lush v1.5.0 uses a unified TOML-based configuration system with XDG Base Directory compliance.

#### Config File Location

```
~/.config/lush/
├── lushrc.toml          # Main configuration (TOML format)
├── lushrc               # Optional shell script (sourced after lushrc.toml)
└── themes/              # Theme files
```

The shell respects `$XDG_CONFIG_HOME` if set, otherwise uses `~/.config`.

#### Config Commands

```bash
# View configuration
config show                     # All sections
config show shell               # Shell options
config show completion          # Completion settings
config show display             # Display settings

# Get values
config get shell.errexit

# Set values
config set shell.errexit true
config set completion.enabled true
config set display.syntax_highlighting true

# Save to file
config save

# Reset defaults
config reset

# Migrate from legacy format
config migrate
```

### Shell Options

Multiple syntax options for setting shell options:

```bash
# setopt/unsetopt (recommended, user-friendly)
setopt errexit            # Enable exit-on-error
setopt extglob            # Enable extended globbing
unsetopt xtrace           # Disable tracing

# config command
config set shell.errexit true

# Traditional POSIX
set -e
set -o errexit
```

Changes persist when you run `config save`.

### TOML Configuration (~/.config/lush/lushrc.toml)

Human-readable TOML format:

```toml
# ~/.config/lush/lushrc.toml

[shell]
mode = "lush"
errexit = false
nounset = false
xtrace = false

# Feature overrides (only non-default values needed)
[shell.features]
extended_glob = true

[history]
enabled = true
size = 10000
file = "~/.local/share/lush/history"
no_dups = true

[display]
syntax_highlighting = true
autosuggestions = true
transient_prompt = false

[prompt]
theme = "default"

[completion]
enabled = true
fuzzy = true
case_sensitive = false
```

### Shell Script Configuration (~/.config/lush/lushrc)

Optional power-user escape hatch, sourced after lushrc.toml:

```bash
# ~/.config/lush/lushrc

# Aliases
alias ll='ls -la'
alias gs='git status'

# Functions
mkcd() {
    mkdir -p "$1" && cd "$1"
}

# Hooks
precmd() {
    # Runs before each prompt
}

# Override TOML settings if needed
setopt extglob
```

### Legacy Format Migration

If you have an existing `~/.lushrc` file, Lush will load it and display a migration notice:

```
lush: Loading configuration from ~/.lushrc (legacy location)
lush: Run 'config save' to migrate to ~/.config/lush/lushrc.toml
```

After running `config save`, your settings are migrated to the new location.

### Environment Variables

```bash
export XDG_CONFIG_HOME=~/.config    # Config directory (default)
export LUSH_THEME=dark
export LUSH_DEBUG=1
export LUSH_DISPLAY_OPTIMIZATION=2
```

---

## Builtin Commands

Lush provides 50 builtin commands.

### POSIX Standard Builtins

```
:          .          alias      bg         break
cd         command    continue   eval       exec
exit       export     false      fc         fg
getopts    hash       jobs       kill       pwd
read       readonly   return     set        shift
times      trap       true       type       ulimit
umask      unalias    unset      wait
```

### Extended Builtins

```
declare    local      let        printf     source
test       [          [[
```

### Lush-Specific Builtins

```
config     display    debug      network    setopt
theme      unsetopt   help
```

### Notable Builtins

**config** - Configuration management

```bash
config show [section]
config get <key>
config set <key> <value>
config save
config reset
```

**debug** - Integrated debugger

```bash
debug on [level]
debug off
debug vars
debug print <var>
debug trace on|off
debug profile on|off|report
```

**display** - Display system control

```bash
display status
display lle diagnostics
display features
display themes
display stats
```

**setopt / unsetopt** - Enable/disable shell options

```bash
setopt                    # List all options
setopt errexit            # Enable option
setopt extglob            # Enable extended globbing
setopt -q extglob         # Query silently
unsetopt xtrace           # Disable option
```

For complete builtin documentation, see [BUILTIN_COMMANDS.md](BUILTIN_COMMANDS.md).

---

## POSIX Compliance

Lush implements all 24 POSIX shell options.

### Shell Options Reference

| Option | Short | Description |
|--------|-------|-------------|
| `allexport` | `-a` | Export all variables |
| `errexit` | `-e` | Exit on command failure |
| `hashall` | `-h` | Hash command locations |
| `ignoreeof` | | Require explicit exit |
| `interactive` | `-i` | Interactive mode |
| `monitor` | `-m` | Job control |
| `noclobber` | `-C` | Don't overwrite files |
| `noexec` | `-n` | Read but don't execute |
| `noglob` | `-f` | Disable globbing |
| `nolog` | | Don't log functions |
| `notify` | `-b` | Report job status immediately |
| `nounset` | `-u` | Error on unset variables |
| `pipefail` | | Pipeline failure detection |
| `posix` | | POSIX compliance mode |
| `privileged` | `-p` | Privileged mode |
| `verbose` | `-v` | Print input lines |
| `vi` | | Vi editing mode |
| `xtrace` | `-x` | Trace execution |

### Setting Options

```bash
# Short form
set -e -u -x

# Combined
set -eux

# Long form
set -o errexit
set -o nounset
set -o xtrace

# Config form
config set shell.errexit true

# Disable
set +e
set +o errexit
config set shell.errexit false
```

---

## Scripting

### Script Structure

```bash
#!/usr/bin/env lush

# Strict mode (recommended)
set -euo pipefail

# Your script here
```

### Error Handling

```bash
# Exit on error
set -e

# Trap errors
trap 'echo "Error on line $LINENO"; exit 1' ERR

# Check commands
if ! command -v git >/dev/null 2>&1; then
    echo "git required" >&2
    exit 1
fi
```

### Functions

```bash
# Standard function
my_function() {
    local arg="$1"
    echo "Processing: $arg"
}

# With nameref (Lush mode)
swap() {
    local -n ref1=$1
    local -n ref2=$2
    local tmp=$ref1
    ref1=$ref2
    ref2=$tmp
}
```

### Portable Scripts

For maximum portability, use POSIX mode:

```bash
#!/usr/bin/env lush
set -o posix

# POSIX-only constructs
# No arrays, no [[]], no process substitution
```

### Debugging Scripts

```bash
#!/usr/bin/env lush

# Debug the whole script
debug on 2

# Or debug specific sections
process_data() {
    debug on
    # complex logic
    debug off
}

# Profile performance
debug profile on
expensive_operation
debug profile report
debug profile off
```

---

## Further Reading

| Document | Description |
|----------|-------------|
| [LLE_GUIDE.md](LLE_GUIDE.md) | Complete LLE documentation |
| [EXTENDED_SYNTAX.md](EXTENDED_SYNTAX.md) | Detailed syntax reference |
| [SHELL_MODES.md](SHELL_MODES.md) | Mode documentation |
| [DEBUGGER_GUIDE.md](DEBUGGER_GUIDE.md) | Debugging reference |
| [BUILTIN_COMMANDS.md](BUILTIN_COMMANDS.md) | All 50 builtins |
| [HOOKS_AND_PLUGINS.md](HOOKS_AND_PLUGINS.md) | Hook system |
| [CONFIG_SYSTEM.md](CONFIG_SYSTEM.md) | Configuration reference |
| [SHELL_OPTIONS.md](SHELL_OPTIONS.md) | All shell options |
