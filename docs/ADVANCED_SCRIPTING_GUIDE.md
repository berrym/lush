# Advanced Scripting Guide

**Professional shell scripting with Lush v1.4.0**

---

## Table of Contents

1. [Script Structure](#script-structure)
2. [Error Handling](#error-handling)
3. [Working with Arrays](#working-with-arrays)
4. [Advanced Functions](#advanced-functions)
5. [Process Management](#process-management)
6. [Data Processing](#data-processing)
7. [Using Hooks](#using-hooks)
8. [Debugging Scripts](#debugging-scripts)
9. [Performance](#performance)
10. [Portability](#portability)

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

## Working with Arrays

### Array Basics

```bash
# Indexed arrays
files=()
files+=("file1.txt")
files+=("file2.txt")

# From glob
scripts=(*.sh)

# From command output
readarray -t lines < file.txt
# Or
mapfile -t lines < file.txt

# Iterate
for file in "${files[@]}"; do
    process "$file"
done

# Check if empty
if [[ ${#files[@]} -eq 0 ]]; then
    echo "No files"
fi
```

### Associative Arrays

```bash
declare -A config

# Load configuration
config[host]="localhost"
config[port]="8080"
config[debug]="false"

# Access
echo "Connecting to ${config[host]}:${config[port]}"

# Check key exists
if [[ -v config[host] ]]; then
    echo "Host configured"
fi

# Iterate
for key in "${!config[@]}"; do
    echo "$key = ${config[$key]}"
done
```

### Array Operations

```bash
# Length
count=${#array[@]}

# Slice
subset=("${array[@]:2:3}")  # 3 elements starting at index 2

# Contains
contains() {
    local item="$1"
    shift
    for element in "$@"; do
        [[ "$element" == "$item" ]] && return 0
    done
    return 1
}

if contains "target" "${array[@]}"; then
    echo "Found"
fi

# Join
join_array() {
    local delimiter="$1"
    shift
    local result="$1"
    shift
    for item in "$@"; do
        result+="${delimiter}${item}"
    done
    echo "$result"
}

result=$(join_array "," "${array[@]}")
```

### Passing Arrays to Functions

```bash
# Pass by expansion
process_files() {
    local files=("$@")
    for file in "${files[@]}"; do
        echo "Processing: $file"
    done
}
process_files "${my_files[@]}"

# Pass by nameref (Lush mode)
process_array() {
    local -n arr=$1
    for item in "${arr[@]}"; do
        echo "Item: $item"
    done
}
process_array my_array
```

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

### Using the Debugger

```bash
#!/usr/bin/env lush
set -euo pipefail

# Enable debugging
debug on 2

# Your script
for file in *.txt; do
    debug print file
    process "$file"
done

debug off
```

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

### POSIX Mode

For maximum portability:

```bash
#!/usr/bin/env lush
set -o posix

# POSIX-only constructs
# No arrays, no [[ ]], no process substitution
```

### Feature Detection

```bash
# Check for feature before using
if [[ -v BASH_VERSION ]] || [[ -v LUSH_VERSION ]]; then
    # Use arrays
    declare -a items
else
    # Fall back to string
    items=""
fi
```

### Portable Constructs

```bash
# Instead of [[ ]]
if [ "$a" = "$b" ]; then
    echo "Equal"
fi

# Instead of arrays
# Use positional parameters
set -- item1 item2 item3
for item in "$@"; do
    echo "$item"
done

# Instead of process substitution
# Use temporary files
cmd1 > /tmp/out1
cmd2 > /tmp/out2
diff /tmp/out1 /tmp/out2
rm /tmp/out1 /tmp/out2
```

---

## See Also

- [EXTENDED_SYNTAX.md](EXTENDED_SYNTAX.md) - Extended language features
- [CONFIGURATION.md](CONFIGURATION.md) - Portability modes
- [DEBUGGER_GUIDE.md](DEBUGGER_GUIDE.md) - Complete debugging reference
- [HOOKS_AND_PLUGINS.md](HOOKS_AND_PLUGINS.md) - Hook system
