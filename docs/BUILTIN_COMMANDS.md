# Builtin Commands Reference

**All 66 registered builtin entries (61 distinct commands) in Lush v1.5.0**

Five entries are aliases that share an implementation: `.` ↔ `source`,
`[` ↔ `test`, `typeset` ↔ `declare`, `readarray` ↔ `mapfile`,
`printenv` ↔ `env`. The canonical registry lives in
`src/builtins/builtins.c` (the static `builtins[]` array).

---

## Table of Contents

1. [Overview](#overview)
2. [POSIX Standard Builtins](#posix-standard-builtins)
3. [Extended Builtins](#extended-builtins)
4. [Lush-Specific Builtins](#lush-specific-builtins)
5. [Quick Reference](#quick-reference)

---

## Overview

Lush provides 61 distinct builtin commands (66 registry entries including
five aliases). These execute within the shell process without spawning
external programs, making them faster and giving them access to shell
internals.

### Builtin Categories

| Category | Commands |
|----------|----------|
| POSIX Standard | `:`, `.`, `break`, `continue`, `eval`, `exec`, `exit`, `export`, `readonly`, `return`, `set`, `shift`, `trap`, `unset` |
| POSIX Utilities | `alias`, `bg`, `cd`, `command`, `disown`, `env`, `fc`, `fg`, `getopts`, `hash`, `jobs`, `printenv`, `pwd`, `read`, `test`, `times`, `type`, `ulimit`, `umask`, `unalias`, `wait` |
| Extended | `declare`, `dirs`, `echo`, `false`, `help`, `history`, `let`, `local`, `mapfile`, `popd`, `print`, `printf`, `pushd`, `readarray`, `return_value`, `shopt`, `source`, `true`, `typeset`, `[` |
| Lush-Specific | `analyze`, `clear`, `config`, `debug`, `display`, `lint`, `mode`, `network`, `setopt`, `terminal`, `unsetopt` |

---

## POSIX Standard Builtins

### `:` (colon)

Null command. Does nothing, returns success.

```bash
:                     # No-op
: ${var:=default}     # Parameter expansion side effects
while :; do           # Infinite loop
    # ...
done
```

### `.` (dot) / `source`

Execute commands from a file in the current shell.

```bash
. ./script.sh         # Execute script.sh
source ~/.bashrc      # Same as .
. config.sh arg1      # With arguments
```

### `break`

Exit from a loop.

```bash
for i in 1 2 3 4 5; do
    if [ $i -eq 3 ]; then
        break           # Exit loop
    fi
done

break 2               # Break out of 2 nested loops
```

### `continue`

Skip to next iteration of a loop.

```bash
for i in 1 2 3 4 5; do
    if [ $i -eq 3 ]; then
        continue        # Skip 3
    fi
    echo $i
done

continue 2            # Continue outer loop
```

### `eval`

Evaluate arguments as shell commands.

```bash
cmd="echo hello"
eval $cmd             # Executes: echo hello

var=PATH
eval echo \$$var      # Echoes value of $PATH
```

### `exec`

Replace shell with command, or redirect file descriptors.

```bash
exec ls               # Replace shell with ls
exec 3< file.txt      # Open file on fd 3
exec 1> output.txt    # Redirect stdout to file
exec 2>&1             # Redirect stderr to stdout
```

### `exit`

Exit the shell.

```bash
exit                  # Exit with last command's status
exit 0                # Exit with success
exit 1                # Exit with failure
```

### `export`

Export variables to environment.

```bash
export VAR=value      # Export with value
export VAR            # Export existing variable
export -p             # List all exports
export -n VAR         # Remove export (keep variable)
```

### `readonly`

Make variables read-only.

```bash
readonly VAR=value    # Create read-only variable
readonly VAR          # Make existing variable read-only
readonly -p           # List read-only variables
```

### `return`

Return from a function.

```bash
my_func() {
    if [ $# -eq 0 ]; then
        return 1      # Return with error
    fi
    return 0          # Return success
}
```

### `set`

Set shell options and positional parameters.

```bash
# Set options
set -e                # Exit on error (errexit)
set -u                # Error on unset variables (nounset)
set -x                # Trace execution (xtrace)
set -n                # Read commands but don't execute (noexec)
set -o errexit        # Long form

# Trap inheritance into functions
set -o errtrace       # ERR trap follows into function bodies (-E)
set -o functrace      # DEBUG/RETURN traps follow into function bodies (-T)
set -o pipefail       # Pipeline fails on any non-zero stage

# Disable options
set +e                # Disable exit on error

# Set positional parameters
set -- arg1 arg2 arg3
echo $1 $2 $3

# Show all variables
set

# Show options
set -o
```

`errtrace` and `functrace` shape the inheritance of the ERR / DEBUG /
RETURN pseudo-signal traps. Without them, an ERR trap fires only at a
function's call site on non-zero return, and DEBUG/RETURN do not fire
inside function bodies -- matching bash's defaults. With them, the
trap follows execution into the body. See [CONFIGURATION.md](CONFIGURATION.md)
for the complete option list.

### `shift`

Shift positional parameters.

```bash
echo $1 $2 $3         # arg1 arg2 arg3
shift
echo $1 $2            # arg2 arg3
shift 2               # Shift by 2
```

### `trap`

Set signal handlers.

```bash
# Trap signals
trap 'echo Interrupted' INT
trap 'cleanup' EXIT
trap '' TERM          # Ignore SIGTERM

# Remove trap
trap - INT

# List traps
trap
```

### `unset`

Remove variables or functions.

```bash
unset VAR             # Remove variable
unset -v VAR          # Remove variable (explicit)
unset -f func         # Remove function
```

---

## POSIX Utilities

### `alias`

Create command aliases.

```bash
alias ll='ls -la'
alias                 # List all aliases
alias ll              # Show specific alias
```

### `bg`

Resume job in background.

```bash
bg                    # Resume most recent job
bg %1                 # Resume job 1
bg %job_name          # Resume by name
```

### `cd`

Change directory.

```bash
cd /path/to/dir       # Absolute path
cd relative/path      # Relative path
cd                    # Home directory
cd -                  # Previous directory ($OLDPWD)
cd ~user              # User's home directory
```

### `command`

Execute command, bypassing functions and aliases.

```bash
command ls            # Run ls, not alias
command -v ls         # Show how ls would be executed
command -V ls         # Verbose description
command -p ls         # Use default PATH
```

### `disown`

Remove jobs from the shell's job list, or mark them so they don't
receive SIGHUP when the shell exits.

```bash
sleep 100 &
disown                # Drop the most-recent job from job control
disown %1             # Drop job 1
disown -h %2          # Keep job 2, but don't send SIGHUP on shell exit
disown -a             # Drop all jobs
```

### `env` / `printenv`

Print or modify the process environment. `printenv` is an alias for
`env` with no command argument.

```bash
env                   # Print all environment variables
env FOO=bar cmd args  # Run cmd with FOO=bar added/overridden
env -i cmd            # Run cmd with an empty environment
printenv              # Same as `env` with no args
printenv FOO          # Print just FOO
```

### `fc`

Fix command - edit and re-execute history entries.

```bash
fc                    # Edit last command in $EDITOR
fc -l                 # List recent history
fc -l -10             # List last 10 commands
fc -s pattern=replace # Substitute and execute
fc 100 110            # Edit range of history
```

### `fg`

Bring job to foreground.

```bash
fg                    # Most recent job
fg %1                 # Job 1
fg %job_name          # By name
```

### `getopts`

Parse command options.

```bash
while getopts "ab:c" opt; do
    case $opt in
        a) echo "Option a" ;;
        b) echo "Option b: $OPTARG" ;;
        c) echo "Option c" ;;
        \?) echo "Invalid option" ;;
    esac
done
shift $((OPTIND - 1))
```

### `hash`

Remember command locations.

```bash
hash                  # List hashed commands
hash -r               # Clear hash table
hash ls               # Hash ls
hash -d ls            # Remove ls from hash
hash -p /usr/bin/ls ls  # Set explicit path
```

### `jobs`

List jobs.

```bash
jobs                  # List all jobs
jobs -l               # Include PIDs
jobs -p               # PIDs only
jobs -r               # Running only
jobs -s               # Stopped only
```

### `pwd`

Print working directory.

```bash
pwd                   # Current directory
pwd -L                # Logical (with symlinks)
pwd -P                # Physical (resolved)
```

### `read`

Read input.

```bash
read var              # Read into var
read -p "Prompt: " var  # With prompt
read -r line          # Raw mode (no backslash escape)
read -t 5 var         # Timeout
read -n 1 char        # Single character
read -s pass          # Silent (passwords)
read -a array         # Into array
```

### `test` / `[`

Evaluate expressions.

```bash
test -f file          # File exists
[ -d dir ]            # Directory exists
[ "$a" = "$b" ]       # String equality
[ $n -eq 5 ]          # Numeric equality
[ -z "$str" ]         # Empty string
[ -n "$str" ]         # Non-empty string
```

See [EXTENDED_SYNTAX.md](EXTENDED_SYNTAX.md) for `[[]]`.

### `times`

Display process times.

```bash
times                 # Show shell and child times
# Output: user_shell system_shell
#         user_children system_children
```

### `type`

Display command type.

```bash
type ls               # ls is /bin/ls
type cd               # cd is a shell builtin
type -t ls            # file
type -a echo          # All locations
type -p ls            # Path only
```

### `ulimit`

Set resource limits.

```bash
ulimit -a             # Show all limits
ulimit -n             # Open files
ulimit -n 1024        # Set open files limit
ulimit -c unlimited   # Unlimited core size
ulimit -v 1000000     # Virtual memory (KB)
```

### `umask`

Set file creation mask.

```bash
umask                 # Show current mask
umask 022             # Set mask (octal)
umask -S              # Symbolic format
umask u=rwx,g=rx,o=rx # Symbolic set
```

### `unalias`

Remove aliases.

```bash
unalias ll            # Remove ll alias
unalias -a            # Remove all aliases
```

### `wait`

Wait for jobs to complete.

```bash
wait                  # Wait for all background jobs
wait $pid             # Wait for specific PID
wait %1               # Wait for job 1
wait -n               # Wait for any job
```

---

## Extended Builtins

### `declare` / `typeset`

Declare variables with attributes.

```bash
declare var=value     # Declare variable
declare -i num=42     # Integer
declare -a arr        # Indexed array (use this form for arrays)
declare -A map        # Associative array
declare -r const=val  # Read-only
declare -x var        # Export
declare -l lower      # Lowercase
declare -u upper      # Uppercase
declare -n ref=other  # Nameref
declare -p var        # Print declaration
declare -f func       # Print function
declare -F            # List function names

# Array literals as arguments
declare -a arr=(a b c)        # Three-element indexed array
declare arr=(a b c)           # Auto-promoted to indexed array
declare data="(literal)"      # Stays scalar (quoted -- not an array)
```

The parser distinguishes the unquoted `name=(...)` array-literal form
from a quoted scalar like `data="(literal)"` via an internal sentinel
prefix on argv (see `src/parser.c`); `declare` and `typeset` route
the array form into `symtable_set_array`, the scalar form into
`symtable_set_var`.

### `dirs`

Display the directory stack maintained by `pushd` / `popd`.

```bash
dirs                  # Print stack on one line
dirs -p               # One entry per line
dirs -v               # Numbered entries
dirs -c               # Clear the stack
```

### `echo`

Display text.

```bash
echo "Hello World"
echo -n "No newline"
echo -e "Tab:\tNewline:\n"
echo -E "Literal \n"   # No escape interpretation
```

### `false`

Return failure status.

```bash
false                 # Returns 1
if false; then        # Never executes
    echo "Never"
fi
```

### `help`

Display builtin help.

```bash
help                  # List all builtins
help cd               # Help for cd
help -s cd            # Short usage
```

### `history`

Command history.

```bash
history               # Show history
history 10            # Last 10 entries
history -c            # Clear history
history -d 5          # Delete entry 5
history -a            # Append to file
history -r            # Read from file
history -w            # Write to file
```

### `let`

Evaluate arithmetic expressions; exits non-zero if the last expression
is 0.

```bash
let x=5+3             # x is set to 8
let "x = 5 + 3"       # Same, with quoted spaces
let x++ y--           # Multiple expressions
let "x > 0" && echo positive
```

### `local`

Declare local variables in functions. The variable dies with the
function scope -- matching bash and zsh, and verified by lush's
storage-unification work (arrays now respect scope identically to
scalars).

```bash
my_func() {
    local var=value         # Local scalar
    local -i num=42         # Local integer
    local -a arr            # Local indexed array (declaration)
    local arr=(a b c)       # Local indexed array (literal)
    local -n ref=$1         # Local nameref
    local data="(literal)"  # Scalar -- quoted (...) is not an array
}
```

The unquoted `local arr=(...)` form is distinguished from
`local data="(...)"` at parser level (see `declare` above); both
forms can coexist in the same function.

### `mapfile` / `readarray`

Read lines from stdin into an indexed array.

```bash
mapfile -t lines < input.txt    # Read file into lines[], strip newlines
mapfile -t -n 10 first10        # Read at most 10 lines
mapfile -t -s 2 rest             # Skip first 2 lines
readarray -t arr < <(seq 1 5)    # Same as mapfile; bash alias
```

### `popd`

Pop a directory off the stack and change to the new top. See `pushd`
for the inverse.

```bash
popd                  # Pop top of stack, cd to new top
popd +1               # Remove entry at offset 1
popd -n               # Pop without changing directory
```

### `print`

Zsh-style print. Like `echo` but with more options.

```bash
print "Hello"               # Print with trailing newline
print -n "no newline"       # Suppress trailing newline
print -l one two three      # One argument per line
print -r "raw \n"           # Disable backslash escape processing
print -u 2 "to stderr"      # Print to fd 2
print -f "%d items\n" 7     # printf-style
print -P "%F{red}red%f"     # Process zsh prompt-style escapes
```

### `printf`

Formatted output.

```bash
printf "Hello %s\n" "World"
printf "%d + %d = %d\n" 2 3 5
printf "%.2f\n" 3.14159
printf "%10s\n" "right"
printf "%-10s\n" "left"
printf "%*s\n" 10 "dynamic"
printf "%.*f\n" 2 3.14159
```

### `pushd`

Push the current directory onto the stack and change to a new
directory. Use `popd` to return, `dirs` to inspect.

```bash
pushd /tmp            # Push pwd, cd /tmp
pushd                 # Swap top two stack entries
pushd +1              # Rotate stack
pushd -n /opt         # Push without changing directory
```

### `return_value`

Set the function return value (replaces the marker-hack
`__LUSH_RETURN__` form). Designed for the typed-function form
(§5.3) that's currently in development; today usable as a marker
inside POSIX-form functions.

```bash
my_func() {
    return_value "computed result"
}
```

### `shopt`

Bash-style shell options. Operates on the same feature matrix as
`setopt`/`unsetopt` (the spelling differs; the effect doesn't).

```bash
shopt                       # List all options with state
shopt -s extglob            # Enable extended globbing
shopt -u nullglob           # Disable nullglob
shopt -p extglob            # Print as `shopt -s extglob`
shopt extglob               # Test current state (exit 0 = on)
```

See [CONFIGURATION.md](CONFIGURATION.md) for the full feature matrix
and the canonical lush spelling.

### `true`

Return success status.

```bash
true                  # Returns 0
while true; do        # Infinite loop
    # ...
done
```

---

## Lush-Specific Builtins

### `analyze`

Run the full script analyzer (info / warnings / errors), the same
machine that powers `--analyze` on the command line and the predictive
type-mismatch warnings in `debug analyze`. Useful as a pre-commit /
pre-deploy gate.

```bash
analyze script.sh                 # Full analysis with default format
analyze --format=json script.sh   # JSON output for tooling
analyze --strict script.sh        # Treat warnings as errors
```

### `clear`

Clear the terminal screen.

```bash
clear                 # Clear screen
```

### `config`

Manage shell configuration.

```bash
# View configuration
config show           # All sections
config show shell     # Shell options
config show completion
config show display

# Get/set values
config get shell.errexit
config set shell.errexit true
config set completion.enabled true

# Persistence
config save           # Save to file
config reset          # Reset to defaults
```

### `debug`

Integrated debugger.

```bash
# Enable/disable
debug on              # Basic debugging
debug on 2            # Verbose
debug on 3            # Trace
debug off             # Disable

# Inspection
debug vars            # All variables
debug print VAR       # Specific variable
debug functions       # List functions

# Tracing
debug trace on        # Enable trace
debug trace off       # Disable trace

# Profiling
debug profile on      # Start profiling
debug profile report  # Show results
debug profile off     # Stop profiling

# Help
debug help            # Full documentation
```

See [DEBUGGER_GUIDE.md](DEBUGGER_GUIDE.md) for complete documentation.

### `display`

Manage the layered display system and the Lush Line Editor (LLE). The
top-level surface and its `lle` sub-router live in
`src/builtins/bin_display.c`; per-subcommand handlers live in
`src/builtins/display/lle_*.c`.

```bash
# Top-level
display status         # Display-controller status snapshot
display features       # Enabled rendering features
display themes         # Available themes
display stats          # Performance counters
display config         # Effective display config
display help           # Inline help
```

#### `display lle` subcommands

```bash
# Status / diagnostics
display lle status              # LLE state + capability snapshot
display lle diagnostics         # LLE health + warnings
display lle reset               # Hard reset; --soft / --terminal variants

# Feature toggles (sugar over config.display.* keys)
display lle autosuggestions on|off
display lle syntax on|off
display lle transient on|off
display lle hot-reload on|off
display lle newline-before on|off
display lle multiline on|off

# Theme + completion
display lle theme [list|set <name>|export]
display lle completion [list|reload|help]

# Keybindings (sugar over ~/.config/lush/keybindings.toml)
display lle keybindings [list|reload|actions]

# History
display lle history [status|dedup|nav-dedup|nav-unique]

# Customization surfaces (LLE Phase 3)
display lle widget  [list|add NAME 'CMD'|remove NAME|show NAME]
display lle hook    [list|add HOOK WIDGET|remove HOOK WIDGET]
display lle segment [list|add NAME VAR|remove NAME|show NAME]
```

The customization trio composes by design: `widget` defines the work,
`hook` decides when to invoke it (line-init / pre-command /
post-command / completion-start / etc.), `segment` surfaces the
result in the prompt by tracking a shell variable. See
[CONFIGURATION.md](CONFIGURATION.md) for the configuration-surface
model these subcommands fit into.

### `lint`

Lint a script for style and correctness issues, optionally applying
the safe automatic fixes. Companion to `analyze` -- where `analyze`
reports everything, `lint` focuses on actionable issues.

```bash
lint script.sh                # Report issues
lint --fix script.sh          # Apply safe automatic fixes
lint --unsafe-fixes script.sh # Apply all fixes, including ones a
                              # reviewer should look at
lint --dry-run script.sh      # Preview fixes without applying
```

### `mode`

Select the active shell mode preset. The four presets (`posix`,
`bash`, `zsh`, `lush`) configure the feature matrix and a small set
of defaults; per SEMANTICS.md they are presentation/preset
configuration over a unified engine, NOT separate engines.

```bash
mode                  # Print the active mode
mode lush             # Switch to lush mode (default)
mode bash             # Switch to bash mode
mode zsh              # Switch to zsh mode
mode posix            # Switch to strict POSIX mode
```

`set -o posix`/`set -o bash`/`set -o zsh`/`set -o lush` are accepted
as bridges, but `mode` is the canonical entry point. See
[CONFIGURATION.md](CONFIGURATION.md) for the four-surface
configuration model.

### `network`

Manage network and SSH hosts.

```bash
network hosts list    # List known hosts
network hosts add hostname  # Add host
network hosts remove hostname  # Remove host
network hosts refresh # Refresh from files
```

### `setopt`

Enable shell options and features.

```bash
# List all options with current state
setopt                # Show all options and their values

# Enable options
setopt errexit        # Exit on error (like set -e)
setopt nounset        # Error on unset variables (like set -u)
setopt xtrace         # Trace execution (like set -x)

# Enable extended features
setopt extglob        # Extended globbing patterns
setopt extended_glob  # Same as extglob (canonical name)
setopt arrays         # Array support
setopt assoc_arrays   # Associative array support
setopt command_sub    # Command substitution
setopt process_sub    # Process substitution
setopt brace_expand   # Brace expansion
setopt extended_test  # [[ ]] test syntax

# Query options
setopt -q extglob     # Silent query (exit status only)
                      # Returns 0 if enabled, 1 if disabled

# Print in re-usable format
setopt -p             # Output suitable for config file
```

**Available Options:**

| Option | Aliases | Description |
|--------|---------|-------------|
| `errexit` | `-e` | Exit immediately on error |
| `nounset` | `-u` | Error on unset variable use |
| `xtrace` | `-x` | Trace command execution |
| `verbose` | `-v` | Print input lines |
| `noclobber` | `-C` | Don't overwrite files with `>` |
| `allexport` | `-a` | Export all variables |
| `notify` | `-b` | Report job status immediately |
| `noglob` | `-f` | Disable pathname expansion |
| `noexec` | `-n` | Read but don't execute |
| `extended_glob` | `extglob` | Extended globbing patterns |
| `arrays` | | Indexed array support |
| `assoc_arrays` | | Associative array support |
| `command_sub` | | `$(...)` command substitution |
| `process_sub` | | `<(...)` and `>(...)` |
| `brace_expand` | `braceexpand` | `{a,b,c}` expansion |
| `extended_test` | | `[[ ]]` conditional syntax |
| `arith_expansion` | | `$((...))` arithmetic |
| `local_vars` | | `local` variable declarations |
| `nameref` | | Name reference variables |

Changes made with `setopt` are persisted when you run `config save`.

See also: `unsetopt`, `set -o`

### `unsetopt`

Disable shell options and features.

```bash
# Disable options
unsetopt errexit      # Don't exit on error
unsetopt xtrace       # Stop tracing

# Disable extended features
unsetopt extglob      # Disable extended globbing
unsetopt brace_expand # Disable brace expansion

# Query before disabling
setopt -q extglob && unsetopt extglob
```

Changes made with `unsetopt` are persisted when you run `config save`.

See also: `setopt`, `set +o`

### `terminal`

Display terminal information.

```bash
terminal              # Terminal info
terminal info         # Detailed info
terminal capabilities # Capability detection
```

---

## Quick Reference

### All 66 Builtins (61 distinct, alphabetical)

```
:           .           [           alias       analyze
bg          break       cd          clear       command
config      continue    debug       declare     dirs
disown      display     echo        env         eval
exec        exit        export      false       fc
fg          getopts     hash        help        history
jobs        let         lint        local       mapfile
mode        network     popd        print       printenv
printf      pushd       pwd         read        readarray
readonly    return      return_value  set       setopt
shift       shopt       source      terminal    test
times       trap        true        type        typeset
ulimit      umask       unalias     unset       unsetopt
wait
```

Five entries are aliases (same underlying impl): `.` ↔ `source`,
`[` ↔ `test`, `typeset` ↔ `declare`, `readarray` ↔ `mapfile`,
`printenv` ↔ `env`. Distinct command count is 61.

### By Purpose

| Purpose | Builtins |
|---------|----------|
| Flow control | `break`, `continue`, `return`, `return_value`, `exit` |
| Loops | `for`, `while`, `until` (keywords, not builtins) |
| Conditionals | `test`, `[`, `if` (keyword) |
| Variables | `declare`, `export`, `local`, `readonly`, `unset`, `typeset`, `let` |
| Functions | `return`, `return_value`, `local`, `declare -f` |
| Jobs | `bg`, `disown`, `fg`, `jobs`, `wait` |
| Signals | `trap` |
| I/O | `echo`, `print`, `printf`, `read`, `mapfile`, `readarray` |
| Directory | `cd`, `pwd`, `pushd`, `popd`, `dirs` |
| History | `fc`, `history` |
| Aliases | `alias`, `unalias` |
| Shell config | `set`, `setopt`, `unsetopt`, `shopt`, `mode`, `config` |
| Commands | `command`, `type`, `hash`, `eval`, `exec`, `.`, `source` |
| Environment | `env`, `printenv`, `export` |
| Debugging | `debug` |
| Static analysis | `analyze`, `lint` |
| Display | `display`, `clear`, `terminal` |
| Resources | `ulimit`, `umask`, `times` |
| Options | `getopts`, `shift` |
| Networking | `network` |

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | General error |
| 2 | Misuse of builtin |
| 126 | Command not executable |
| 127 | Command not found |
| 128+n | Killed by signal n |

---

## See Also

- [USER_GUIDE.md](USER_GUIDE.md) - Complete shell reference
- [CONFIGURATION.md](CONFIGURATION.md) - Shell option reference
- [COMPLETION_SYSTEM.md](COMPLETION_SYSTEM.md) - Context-aware completions
- [DEBUGGER_GUIDE.md](DEBUGGER_GUIDE.md) - Debugging reference
