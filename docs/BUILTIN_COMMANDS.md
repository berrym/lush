# Builtin Commands Reference

**All 50 shell builtin commands in Lush v1.5.0**

---

## Table of Contents

1. [Overview](#overview)
2. [POSIX Standard Builtins](#posix-standard-builtins)
3. [Extended Builtins](#extended-builtins)
4. [Lush-Specific Builtins](#lush-specific-builtins)
5. [Quick Reference](#quick-reference)

---

## Overview

Lush provides 48 builtin commands. These execute within the shell process without spawning external programs, making them faster and giving them access to shell internals.

### Builtin Categories

| Category | Commands |
|----------|----------|
| POSIX Standard | `:`, `.`, `break`, `continue`, `eval`, `exec`, `exit`, `export`, `readonly`, `return`, `set`, `shift`, `trap`, `unset` |
| POSIX Utilities | `alias`, `bg`, `cd`, `command`, `fc`, `fg`, `getopts`, `hash`, `jobs`, `pwd`, `read`, `test`, `times`, `type`, `ulimit`, `umask`, `unalias`, `wait` |
| Extended | `declare`, `echo`, `false`, `help`, `history`, `local`, `printf`, `source`, `true`, `typeset`, `[` |
| Lush-Specific | `clear`, `config`, `debug`, `display`, `network`, `setopt`, `terminal`, `unsetopt` |

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
set -e                # Exit on error
set -u                # Error on unset variables
set -x                # Trace execution
set -o errexit        # Long form

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

See [CONFIGURATION.md](CONFIGURATION.md) for all options.

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
declare -a arr        # Indexed array
declare -A map        # Associative array
declare -r const=val  # Read-only
declare -x var        # Export
declare -l lower      # Lowercase
declare -u upper      # Uppercase
declare -n ref=other  # Nameref
declare -p var        # Print declaration
declare -f func       # Print function
declare -F            # List function names
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

### `local`

Declare local variables in functions.

```bash
my_func() {
    local var=value   # Local to function
    local -i num=42   # Local integer
    local -a arr      # Local array
    local -n ref=$1   # Local nameref
}
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

Control display system.

```bash
display status        # System status
display lle diagnostics  # LLE status
display features      # Enabled features
display themes        # Available themes
display stats         # Performance stats
display config        # Configuration
display help          # Full documentation
```

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

### All 50 Builtins

```
:           .           [           alias       bg
break       cd          clear       command     config
continue    debug       declare     display     echo
eval        exec        exit        export      false
fc          fg          getopts     hash        help
history     jobs        local       network     printf
pwd         read        readonly    return      set
setopt      shift       source      terminal    test
times       trap        true        type        typeset
ulimit      umask       unalias     unset       unsetopt
wait
```

### By Purpose

| Purpose | Builtins |
|---------|----------|
| Flow control | `break`, `continue`, `return`, `exit` |
| Loops | `for`, `while`, `until` (keywords, not builtins) |
| Conditionals | `test`, `[`, `if` (keyword) |
| Variables | `declare`, `export`, `local`, `readonly`, `unset`, `typeset` |
| Functions | `return`, `local`, `declare -f` |
| Jobs | `bg`, `fg`, `jobs`, `wait` |
| Signals | `trap` |
| I/O | `echo`, `printf`, `read` |
| Directory | `cd`, `pwd` |
| History | `fc`, `history` |
| Aliases | `alias`, `unalias` |
| Shell config | `set`, `setopt`, `unsetopt`, `config` |
| Commands | `command`, `type`, `hash`, `eval`, `exec`, `.`, `source` |
| Debugging | `debug` |
| Display | `display`, `clear`, `terminal` |
| Resources | `ulimit`, `umask`, `times` |
| Options | `getopts`, `shift` |

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
