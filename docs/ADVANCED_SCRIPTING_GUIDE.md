# Advanced Scripting Guide

**Professional shell scripting with Lush v1.5.0**

This guide is aimed at script authors who already know one of bash,
zsh, or POSIX `sh`. It focuses on the parts of lush you cannot get
from those shells: the typed value model (scalar / list / map as
first-class kinds), the polyglot spelling layer
(`${var^^}` and `${(U)var}` both work), the predictive static
analyzer, and the integrated debugger.

If something here surprises you, the authoritative references are
[SEMANTICS.md](SEMANTICS.md) (engine rules), [CONFIGURATION.md](CONFIGURATION.md)
(the four config surfaces), and [PHILOSOPHY.md](PHILOSOPHY.md) (the
design contracts the language is held to).

---

## Table of Contents

1. [The Value Model You Must Know](#the-value-model-you-must-know)
2. [Script Structure](#script-structure)
3. [Error Handling](#error-handling)
4. [Working with Arrays (Lists)](#working-with-arrays-lists)
5. [Associative Arrays (Maps)](#associative-arrays-maps)
6. [Parameter Expansion: Polyglot Spellings](#parameter-expansion-polyglot-spellings)
7. [Advanced Functions](#advanced-functions)
8. [Process Management](#process-management)
9. [Data Processing](#data-processing)
10. [Using Hooks](#using-hooks)
11. [Debugging Scripts](#debugging-scripts)
12. [Performance](#performance)
13. [Portability](#portability)

---

## The Value Model You Must Know

Lush's executor distinguishes three first-class value kinds (see
SEMANTICS section 7):

- **Scalar** -- a string (NFC-normalized; lengths are grapheme-aware)
- **List** -- an indexed sequence; `${arr[@]}` / `${arr[*]}` are the
  presentation operators
- **Map** -- a key/value table; `${(k)m}` / `${(v)m}` /
  `${!m[@]}` / `${m[@]}` present keys or values

Two engine rules govern interactions and are non-negotiable:

1. **No implicit list-to-string coercion** (section 3.4). Writing a
   bare `${arr}` -- a List in a Scalar slot -- is a **type error**,
   not "the first element," and not "the elements joined by space."
   The shell raises `SHELL_ERR_TYPE_MISMATCH` and the offending
   command exits non-zero with a structured-error diagnostic. There
   is no silent flattening.

2. **The presentation operator is mandatory** (section 3.9). To use a
   List as a Scalar, you must say *how*:

   ```bash
   arr=(alpha beta gamma)

   echo "${arr[@]}"   # OK -- "alpha" "beta" "gamma" as separate words
   echo "${arr[*]}"   # OK -- "alpha beta gamma" as one IFS-joined string
   echo "${arr[0]}"   # OK -- single element by index
   echo "$arr"        # TYPE ERROR -- List in Scalar slot
   echo "${arr}"      # TYPE ERROR (same)
   ```

   This rule extends to assignments and parameters:

   ```bash
   first="${arr[0]}"  # OK
   joined="${arr[*]}" # OK
   broken="${arr}"    # TYPE ERROR
   ```

This is the single largest difference between lush and bash/zsh, and
the static analyzer flags violations *before* you run the script --
see [Debugging Scripts](#debugging-scripts) below for `debug analyze`.

If a section of code reads as "but bash lets me do this," check the
analyzer output. The error message will name the kind and the slot.

---

---

## Script Structure

### Standard Header

```bash
#!/usr/bin/env lush

# script.sh - Description of what the script does
# Usage: script.sh [options] arguments
# Author: Your Name

set -euo pipefail

# Constants
readonly SCRIPT_NAME="${0##*/}"
readonly SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Configuration
DEBUG=${DEBUG:-false}
VERBOSE=${VERBOSE:-false}
```

### Strict Mode

Always use strict mode for robust scripts:

```bash
set -e          # Exit on error
set -u          # Error on unset variables
set -o pipefail # Pipeline failure propagation

# Or combined
set -euo pipefail
```

### Script Organization

```bash
#!/usr/bin/env lush
set -euo pipefail

#----------------------------------------------------------
# Configuration
#----------------------------------------------------------

readonly VERSION="1.0.0"
readonly CONFIG_FILE="${HOME}/.myconfig"

#----------------------------------------------------------
# Functions
#----------------------------------------------------------

usage() {
    cat <<EOF
Usage: ${0##*/} [OPTIONS] COMMAND

Options:
    -h, --help      Show this help
    -v, --verbose   Verbose output
    -d, --debug     Debug mode

Commands:
    start           Start the service
    stop            Stop the service
    status          Show status
EOF
}

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >&2
}

die() {
    log "ERROR: $*"
    exit 1
}

#----------------------------------------------------------
# Main
#----------------------------------------------------------

main() {
    local verbose=false
    
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help)
                usage
                exit 0
                ;;
            -v|--verbose)
                verbose=true
                shift
                ;;
            -*)
                die "Unknown option: $1"
                ;;
            *)
                break
                ;;
        esac
    done
    
    [[ $# -eq 0 ]] && die "Command required"
    
    case "$1" in
        start)  do_start ;;
        stop)   do_stop ;;
        status) do_status ;;
        *)      die "Unknown command: $1" ;;
    esac
}

main "$@"
```

---

## Error Handling

### Exit on Error

```bash
set -e  # Exit immediately on error

# Some commands are expected to fail
if ! command -v optional_tool >/dev/null 2>&1; then
    echo "optional_tool not found, using fallback"
fi

# Or use || true for commands that may fail
grep pattern file.txt || true
```

### Error Traps

```bash
# Cleanup on exit
cleanup() {
    rm -f "$TEMP_FILE"
    [[ -n "${PID:-}" ]] && kill "$PID" 2>/dev/null
}
trap cleanup EXIT

# Handle errors
on_error() {
    echo "Error on line $1" >&2
    exit 1
}
trap 'on_error $LINENO' ERR

# Handle interrupts
on_interrupt() {
    echo "Interrupted" >&2
    exit 130
}
trap on_interrupt INT TERM
```

### Error Functions

```bash
# Die with message
die() {
    echo "ERROR: $*" >&2
    exit 1
}

# Die with usage
die_usage() {
    echo "ERROR: $*" >&2
    usage >&2
    exit 1
}

# Warning (continue)
warn() {
    echo "WARNING: $*" >&2
}

# Usage
[[ $# -lt 1 ]] && die_usage "Missing argument"
[[ -f "$1" ]] || die "File not found: $1"
```

### Validating Input

```bash
validate_number() {
    local value="$1"
    [[ "$value" =~ ^[0-9]+$ ]] || die "Invalid number: $value"
}

validate_file() {
    local file="$1"
    [[ -f "$file" ]] || die "File not found: $file"
    [[ -r "$file" ]] || die "Cannot read: $file"
}

validate_directory() {
    local dir="$1"
    [[ -d "$dir" ]] || die "Directory not found: $dir"
    [[ -w "$dir" ]] || die "Cannot write to: $dir"
}
```

---

## Working with Arrays (Lists)

In lush a `name=(...)` literal binds `name` to a value of kind List,
stored in the same per-scope symbol table as scalars (SEMANTICS
section 7). The `[[ -v name ]]` predicate, `unset name`,
function-local declarations, and the debugger all see Lists
identically to Scalars -- there is no side-table.

### List Basics

```bash
# Empty list
files=()

# Append
files+=("file1.txt")
files+=("file2.txt")

# From glob -- empty list, not the literal pattern, if no match
scripts=(*.sh)

# From command output -- mapfile and readarray are equivalent
mapfile -t lines < file.txt
readarray -t lines < file.txt

# Iterate -- always use "${arr[@]}" with quotes
for file in "${files[@]}"; do
    process "$file"
done

# Length and empty check
count=${#files[@]}
if (( ${#files[@]} == 0 )); then
    echo "No files"
fi
```

### Function-local lists

```bash
collect() {
    local arr=(one two three)   # OK -- parser-level array literal
    local -a others             # OK -- declare empty local list
    others+=(more elements)
    echo "${arr[@]} ${others[@]}"
}
```

Function-local Lists obey the scope-chain walk: they shadow any
global `arr`, and `unset arr` inside the function clears only the
local binding.

### List Operations

```bash
# Slice -- 3 elements starting at index 2
subset=("${array[@]:2:3}")

# Membership
contains() {
    local needle="$1"
    shift
    for element in "$@"; do
        [[ "$element" == "$needle" ]] && return 0
    done
    return 1
}

if contains "target" "${array[@]}"; then
    echo "Found"
fi

# Join with a delimiter
join_array() {
    local delimiter="$1"
    shift
    local result="$1"
    shift
    for item in "$@"; do
        result+="${delimiter}${item}"
    done
    print -- "$result"
}

result=$(join_array "," "${array[@]}")
```

### Passing Lists to Functions

```bash
# Pass by expansion (positional parameters)
process_files() {
    local files=("$@")
    for file in "${files[@]}"; do
        echo "Processing: $file"
    done
}
process_files "${my_files[@]}"

# Pass by nameref -- works in every lush mode (bash, zsh, lush)
# because nameref is canonical lush, not a mode-gated feature
process_array() {
    local -n arr=$1
    for item in "${arr[@]}"; do
        echo "Item: $item"
    done
}
process_array my_array
```

---

## Associative Arrays (Maps)

```bash
declare -A config
config[host]=localhost
config[port]=8080
config[debug]=false

# Read
echo "Connecting to ${config[host]}:${config[port]}"

# Key existence (works on both Maps and Lists)
if [[ -v config[host] ]]; then
    echo "Host configured"
fi

# Iterate by key
for key in "${!config[@]}"; do
    echo "$key = ${config[$key]}"
done

# Zsh-style flags work too -- both spellings, one engine
for key in "${(k)config}"; do
    echo "$key"
done
for value in "${(v)config}"; do
    echo "$value"
done
```

The type-mismatch rule applies to Maps as well: `echo "$config"` is
a TYPE ERROR (Map in Scalar slot). Always use a presentation
operator (`${config[key]}`, `${!config[@]}`, `${config[@]}`,
`${(k)config}`, `${(v)config}`, `${(kv)config}`).

---

## Parameter Expansion: Polyglot Spellings

Lush accepts bash-style and zsh-style parameter expansion as
spellings of the same canonical operations (PHILOSOPHY section 2).
The pairs below all route to one implementation; choose by
readability. Flags are recognised in all modes -- lush does not
gate them behind `mode zsh`.

| Operation | Bash spelling | Zsh spelling |
|-----------|---------------|--------------|
| Uppercase | `${var^^}` | `${(U)var}` |
| Lowercase | `${var,,}` | `${(L)var}` |
| Title-case (first char) | `${var^}` | `${(C)var}` |
| Sort List | (none) | `${(o)arr}` |
| Map keys | `${!m[@]}` | `${(k)m}` |
| Quote-for-reuse | `${var@Q}` | (none) |

Notes on composability:

- Single-flag forms above are stable and tested.
- Multi-flag composition (`${(Uo)arr}`) is recognised at the parser
  level but a small set of combinations still interacts awkwardly
  with `[@]` presentation; if you hit a `SHELL_ERR_TYPE_MISMATCH`
  from a composed flag, file an issue with the exact expression --
  it's a known maturation area, not by design.
- The bash transformation operators (`@Q`, `@E`, `@P`, `@A`, `@K`,
  `@a`) are supported; the zsh `(q)` flag family is a partial
  overlap and is being rolled out incrementally.

---

## Advanced Functions

### Local Variables

```bash
calculate() {
    local -i a=$1 b=$2
    local -i result
    result=$((a + b))
    echo "$result"
}
```

### Namerefs

```bash
# Return multiple values
get_dimensions() {
    local -n width_ref=$1
    local -n height_ref=$2
    width_ref=1920
    height_ref=1080
}

get_dimensions w h
echo "${w}x${h}"

# Modify caller's array
append_items() {
    local -n arr_ref=$1
    shift
    arr_ref+=("$@")
}

items=()
append_items items "a" "b" "c"
```

### Function Libraries

```bash
# lib/string.sh
string_trim() {
    local str="$1"
    str="${str#"${str%%[![:space:]]*}"}"
    str="${str%"${str##*[![:space:]]}"}"
    echo "$str"
}

string_upper() {
    echo "${1^^}"
}

string_lower() {
    echo "${1,,}"
}

# lib/array.sh
array_contains() {
    local needle="$1"
    shift
    for item in "$@"; do
        [[ "$item" == "$needle" ]] && return 0
    done
    return 1
}

# main.sh
source lib/string.sh
source lib/array.sh
```

---

## Process Management

### Background Jobs

```bash
# Start background job
long_task &
pid=$!

# Wait for completion
wait "$pid"
status=$?

# Multiple jobs
pids=()
for file in *.data; do
    process_file "$file" &
    pids+=($!)
done

# Wait for all
for pid in "${pids[@]}"; do
    wait "$pid"
done
```

### Process Substitution

```bash
# Compare outputs
diff <(ls dir1) <(ls dir2)

# Feed to command
while read -r line; do
    echo "Line: $line"
done < <(some_command)

# Multiple inputs
paste <(cut -f1 file1) <(cut -f2 file2)
```

### Timeouts

```bash
# Timeout command
if ! timeout 30 long_running_command; then
    echo "Command timed out"
fi

# Custom timeout
run_with_timeout() {
    local timeout=$1
    shift
    
    "$@" &
    local pid=$!
    
    (
        sleep "$timeout"
        kill -TERM "$pid" 2>/dev/null
    ) &
    local killer=$!
    
    wait "$pid"
    local status=$?
    
    kill "$killer" 2>/dev/null
    wait "$killer" 2>/dev/null
    
    return $status
}
```

---

## Data Processing

### Text Processing

```bash
# Line-by-line processing
while IFS= read -r line; do
    # Process line
    [[ "$line" =~ ^# ]] && continue  # Skip comments
    [[ -z "$line" ]] && continue     # Skip empty
    process "$line"
done < input.txt

# Field processing
while IFS=: read -r user _ uid gid _ home shell; do
    echo "User $user (UID $uid) uses $shell"
done < /etc/passwd

# CSV processing
while IFS=, read -r name email phone; do
    echo "Contact: $name <$email>"
done < contacts.csv
```

### JSON with jq

```bash
# Extract field
name=$(jq -r '.name' data.json)

# Process array
jq -c '.items[]' data.json | while read -r item; do
    id=$(echo "$item" | jq -r '.id')
    process_item "$id"
done

# Build JSON
generate_report() {
    jq -n \
        --arg date "$(date -Iseconds)" \
        --arg host "$(hostname)" \
        --argjson count "$item_count" \
        '{date: $date, host: $host, items: $count}'
}
```

### Configuration Files

```bash
# INI-style parsing
parse_ini() {
    local file="$1"
    local section=""
    
    while IFS='=' read -r key value; do
        # Remove leading/trailing whitespace
        key="${key#"${key%%[![:space:]]*}"}"
        key="${key%"${key##*[![:space:]]}"}"
        
        case "$key" in
            \[*\])
                section="${key:1:-1}"
                ;;
            ''|\#*)
                continue
                ;;
            *)
                echo "${section}_${key}=${value}"
                ;;
        esac
    done < "$file"
}

# Usage
eval "$(parse_ini config.ini)"
echo "Database host: $database_host"
```

---

## Using Hooks

### Command Timing

```bash
# In ~/.lushrc
declare -A _cmd_stats

preexec() {
    _cmd_start=$(date +%s.%N)
    _cmd_text="$1"
}

precmd() {
    if [[ -n "${_cmd_start:-}" ]]; then
        local end=$(date +%s.%N)
        local elapsed=$(echo "$end - $_cmd_start" | bc)
        
        if (( $(echo "$elapsed > 5" | bc -l) )); then
            echo "Command took ${elapsed}s"
        fi
        
        # Track statistics
        local cmd="${_cmd_text%% *}"
        _cmd_stats[$cmd]=$((${_cmd_stats[$cmd]:-0} + 1))
        
        unset _cmd_start
    fi
}
```

### Directory Hooks

```bash
# Auto-activate environments
chpwd() {
    # Python
    if [[ -f "venv/bin/activate" ]]; then
        source venv/bin/activate
    elif [[ -n "${VIRTUAL_ENV:-}" ]]; then
        deactivate 2>/dev/null
    fi
    
    # Node.js
    if [[ -f ".nvmrc" ]]; then
        nvm use 2>/dev/null
    fi
    
    # Load local environment
    if [[ -f ".env" ]]; then
        set -a
        source .env
        set +a
    fi
}
```

---

## Debugging Scripts

### Predictive analysis -- catch errors before running

Run the analyzer over a script to surface type-mismatch, unquoted
expansion, and exit-status issues at edit time:

```bash
debug analyze ./myscript.sh
# or, as a standalone builtin without entering debug mode
analyze ./myscript.sh
lint    ./myscript.sh
```

The analyzer's **type** category is unique to lush: it walks the AST
and flags SEMANTICS section 3.9 type-mismatch sites (`echo "$arr"`
where `arr` is a List, etc.) statically. The full reference is
[DEBUGGER_GUIDE.md](DEBUGGER_GUIDE.md).

### Interactive break-and-step

```bash
#!/usr/bin/env lush
set -euo pipefail

debug break add script.sh 12       # halt at line 12
source script.sh                    # run -- breakpoint fires
# (lush-debug) prompt appears
#   t arr        -> kind only (List / Scalar / Map / Func / Nameref)
#   print arr    -> full contents with kind label
#   next         -> depth-aware step over
#   out          -> step out of current function frame
#   continue     -> resume
```

The break prompt is the LLE-driven `(lush-debug)` prompt -- history,
Ctrl-R search, and tab completion all work, with a framed left-gutter
UI distinguishing debugger output from script output.

### Profiling

```bash
#!/usr/bin/env lush

debug profile on

# Code to profile
expensive_function
another_function

debug profile report
debug profile off
```

### Trace Mode

```bash
# Enable trace in script
set -x

# Or for specific section
set -x
risky_operation
set +x

# With custom prefix
PS4='+ ${BASH_SOURCE}:${LINENO}: '
set -x
```

### Debug Functions

```bash
debug_log() {
    [[ "${DEBUG:-false}" == "true" ]] || return 0
    echo "[DEBUG] $*" >&2
}

debug_var() {
    [[ "${DEBUG:-false}" == "true" ]] || return 0
    local var="$1"
    local -n ref=$var
    echo "[DEBUG] $var = $ref" >&2
}

# Usage
DEBUG=true
debug_log "Starting process"
debug_var my_variable
```

---

## Performance

### Avoid Subshells

```bash
# BAD: Creates subshell
output=$(cat file.txt)

# GOOD: No subshell
output=$(< file.txt)

# BAD: Subshell in loop
cat file.txt | while read -r line; do
    count=$((count + 1))  # Lost after loop
done

# GOOD: No subshell
while read -r line; do
    count=$((count + 1))
done < file.txt
```

### Use Builtins

```bash
# BAD: External command
length=$(echo "$string" | wc -c)

# GOOD: Builtin
length=${#string}

# BAD: External command
basename=$(basename "$path")

# GOOD: Parameter expansion
basename=${path##*/}

# BAD: External command
dirname=$(dirname "$path")

# GOOD: Parameter expansion
dirname=${path%/*}
```

### Batch Operations

```bash
# BAD: Many processes
for file in *.txt; do
    grep pattern "$file"
done

# GOOD: Single process
grep pattern *.txt

# BAD: Repeated lookups
for user in $(cat users.txt); do
    id "$user"
done

# GOOD: Single command
xargs -I{} id {} < users.txt
```

---

## Portability

### POSIX mode is a preset, not a restriction

`mode posix` (or its bash-bridge alias `set -o posix`) loads
POSIX-conforming defaults, but **lush features remain available**
(PHILOSOPHY section 4). Arrays, `[[ ]]`, process substitution, the
debugger -- none of these are turned off by selecting POSIX mode.
You are in lush, not `dash`.

What POSIX mode does change is the *defaults*: word-splitting, glob
handling, `echo` semantics, alias expansion, and so on, all
configure to POSIX-conforming values. The polyglot translation layer
also narrows -- `set -o pipefail` is still recognised because POSIX
spells options the same way, but the bash-only `set -o privileged`
or zsh-only `setopt extended_glob` will not be silently translated.

```bash
#!/usr/bin/env lush
mode posix      # POSIX-conforming defaults; lush features still present
```

### Running scripts on other shells

If a script must run on `bash`, `dash`, or `/bin/sh` (not just lush),
write it within the intersection POSIX defines:

```bash
# Use the test builtin (works in every shell, including POSIX sh)
if [ "$a" = "$b" ]; then
    echo "Equal"
fi

# Use positional parameters in place of arrays
set -- item1 item2 item3
for item in "$@"; do
    echo "$item"
done

# Use temporary files in place of process substitution
cmd1 > /tmp/out1
cmd2 > /tmp/out2
diff /tmp/out1 /tmp/out2
rm /tmp/out1 /tmp/out2
```

Detect lush vs another shell from the executable name when needed:

```bash
case "${0##*/}" in
    lush|*lush*) is_lush=1 ;;
    *)          is_lush=0 ;;
esac
```

(There is no `LUSH_VERSION` environment variable -- the build-time
constant lives only in the compiled binary. Use `lush --version`
from a subshell if you need the version string.)

---

## See Also

- [EXTENDED_SYNTAX.md](EXTENDED_SYNTAX.md) - Extended language features
- [CONFIGURATION.md](CONFIGURATION.md) - Portability modes
- [DEBUGGER_GUIDE.md](DEBUGGER_GUIDE.md) - Complete debugging reference
- [HOOKS_AND_PLUGINS.md](HOOKS_AND_PLUGINS.md) - Hook system
