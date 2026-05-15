# Extended Shell Syntax

**Modern shell features beyond POSIX**

Lush mode (the default) and Bash/Zsh compatibility modes support extended syntax that goes beyond the POSIX shell specification. This document covers all extended language features.

---

## Table of Contents

1. [Arrays](#arrays)
2. [Arithmetic](#arithmetic)
3. [Extended Test](#extended-test)
4. [Process Substitution](#process-substitution)
5. [Parameter Expansion](#parameter-expansion)
6. [Extended Globbing](#extended-globbing)
7. [Glob Qualifiers](#glob-qualifiers)
8. [Control Flow Extensions](#control-flow-extensions)
9. [Functions](#functions)
10. [Mode Requirements](#mode-requirements)

---

## Arrays

Lush supports both indexed and associative arrays.

### Indexed Arrays

```bash
# Declaration and initialization
fruits=(apple banana cherry)
declare -a numbers

# Individual assignment
fruits[0]="apple"
fruits[3]="date"      # Sparse arrays allowed

# Append
fruits+=(elderberry)

# Access single element
echo "${fruits[0]}"   # apple

# Access all elements
echo "${fruits[@]}"   # apple banana cherry date elderberry
echo "${fruits[*]}"   # Same, but as single string in quotes

# Array length
echo "${#fruits[@]}"  # 5

# All indices
echo "${!fruits[@]}"  # 0 1 2 3 4

# Slicing
echo "${fruits[@]:1:2}"   # banana cherry (start at 1, count 2)
echo "${fruits[@]:2}"     # cherry date elderberry (from index 2)
echo "${fruits[@]: -2}"   # date elderberry (last 2, note space)

# Negative indexing
echo "${fruits[-1]}"      # elderberry (last element)
echo "${fruits[-2]}"      # date

# Delete element
unset 'fruits[1]'

# Delete entire array
unset fruits
```

### Associative Arrays

```bash
# Declaration REQUIRED
declare -A colors

# Assignment
colors[apple]="red"
colors[banana]="yellow"
colors[cherry]="red"

# Compound assignment
declare -A sizes=([small]=1 [medium]=5 [large]=10)

# Access value
echo "${colors[apple]}"   # red

# All values
echo "${colors[@]}"       # red yellow red (order undefined)

# All keys
echo "${!colors[@]}"      # apple banana cherry

# Check if key exists
if [[ -v colors[apple] ]]; then
    echo "Apple has a color"
fi

# Number of elements
echo "${#colors[@]}"      # 3

# Delete element
unset 'colors[banana]'

# Iterate
for fruit in "${!colors[@]}"; do
    echo "$fruit is ${colors[$fruit]}"
done
```

### Array Operations

```bash
# Copy array
original=(a b c)
copy=("${original[@]}")

# Merge arrays
merged=("${array1[@]}" "${array2[@]}")

# Filter with pattern
files=(*.txt)
scripts=("${files[@]/*.sh/}")  # Pattern substitution on all

# Map operation (using loop)
numbers=(1 2 3 4 5)
doubled=()
for n in "${numbers[@]}"; do
    doubled+=($((n * 2)))
done

# Check if array is empty
if [[ ${#array[@]} -eq 0 ]]; then
    echo "Array is empty"
fi
```

---

## Arithmetic

Lush provides several ways to perform arithmetic.

### Arithmetic Expansion

Returns the result as text:

```bash
result=$((5 + 3))           # 8
result=$((count * 2))       # Uses variable
result=$((a > b ? a : b))   # Ternary operator
```

### Arithmetic Command

Returns exit status (0 for non-zero, 1 for zero):

```bash
(( count++ ))               # Increment
(( total += value ))        # Add and assign

if (( count > 10 )); then
    echo "Over ten"
fi

# As a loop condition
while (( i < 10 )); do
    echo $i
    (( i++ ))
done
```

### let Builtin

```bash
let count=count+1
let "total = a + b"
let x=5 y=10 z=x+y
```

### Operators

| Category | Operators |
|----------|-----------|
| Basic | `+ - * / %` |
| Exponentiation | `**` |
| Increment/Decrement | `++ --` (pre and post) |
| Comparison | `< > <= >= == !=` |
| Logical | `&& \|\| !` |
| Bitwise | `& \| ^ ~ << >>` |
| Assignment | `= += -= *= /= %= &= \|= ^= <<= >>=` |
| Ternary | `? :` |
| Comma | `,` (evaluate both, return second) |
| Grouping | `( )` |

### Arithmetic Expressions

```bash
# Variables don't need $
x=5
echo $((x + 1))       # 6

# But $ works too
echo $(($x + 1))      # 6

# Complex expressions
echo $((2 ** 10))     # 1024
echo $((255 & 0x0f))  # 15
echo $((1 << 4))      # 16

# Floating point requires external tool
# Arithmetic is integer-only in shell
```

---

## Extended Test

The `[[` compound command provides enhanced testing.

### Basic Syntax

```bash
if [[ condition ]]; then
    commands
fi
```

### String Tests

```bash
# Equality
[[ $str == "value" ]]
[[ $str = "value" ]]      # Same as ==

# Inequality
[[ $str != "value" ]]

# Pattern matching (glob)
[[ $file == *.txt ]]      # No quotes around pattern!
[[ $name == [A-Z]* ]]     # Starts with uppercase

# Lexicographic comparison
[[ $a < $b ]]             # a sorts before b
[[ $a > $b ]]             # a sorts after b

# Empty/non-empty
[[ -z $str ]]             # True if empty
[[ -n $str ]]             # True if non-empty
```

### Regex Matching

```bash
# =~ operator
if [[ $email =~ ^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$ ]]; then
    echo "Valid email"
fi

# Captured groups in BASH_REMATCH
if [[ $version =~ ([0-9]+)\.([0-9]+)\.([0-9]+) ]]; then
    major="${BASH_REMATCH[1]}"
    minor="${BASH_REMATCH[2]}"
    patch="${BASH_REMATCH[3]}"
fi

# Pattern must be unquoted
pattern='^[0-9]+$'
[[ $str =~ $pattern ]]    # Works
[[ $str =~ "^[0-9]+$" ]]  # Literal string, not regex!
```

### Logical Operators

```bash
# AND
[[ -f $file && -r $file ]]

# OR
[[ $opt == "yes" || $opt == "y" ]]

# NOT
[[ ! -d $dir ]]

# Grouping
[[ ( $a || $b ) && $c ]]
```

### File Tests

All standard file tests work in `[[`:

```bash
[[ -e $path ]]    # Exists
[[ -f $file ]]    # Regular file
[[ -d $dir ]]     # Directory
[[ -L $link ]]    # Symbolic link
[[ -r $file ]]    # Readable
[[ -w $file ]]    # Writable
[[ -x $file ]]    # Executable
[[ -s $file ]]    # Size > 0
[[ $a -nt $b ]]   # a newer than b
[[ $a -ot $b ]]   # a older than b
```

### Advantages Over [ ]

```bash
# No word splitting or globbing
file="name with spaces.txt"
[[ -f $file ]]    # Works! No quotes needed

# No need to quote empty variables
[[ $possibly_empty == "" ]]   # Safe

# < and > don't need escaping
[[ $a < $b ]]     # String comparison
[ "$a" \< "$b" ]  # Requires escaping in [ ]

# Pattern matching
[[ $str == pattern* ]]  # Works
[ "$str" == pattern* ]  # Doesn't work as expected
```

---

## Process Substitution

Treat command output as a file.

### Input Process Substitution

`<(command)` - Command output as a file:

```bash
# Compare two directory listings
diff <(ls dir1) <(ls dir2)

# Feed command output to program expecting file
wc -l <(find . -name "*.c")

# Process multiple sources
paste <(cut -f1 file1) <(cut -f2 file2)

# Sort and compare
comm <(sort file1) <(sort file2)
```

### Output Process Substitution

`>(command)` - Write to command as if to file:

```bash
# Tee to multiple destinations
echo "data" | tee >(gzip > file.gz) >(wc -l > count.txt)

# Log while processing
command 2> >(logger -t myapp)

# Transform output
tar cf >(gzip > archive.tar.gz) files/
```

### How It Works

Process substitution creates a named pipe (FIFO) or uses `/dev/fd/N`:

```bash
echo <(echo hello)
# Output: /dev/fd/63 (or similar)
```

### Use Cases

```bash
# Source from dynamic content
source <(generate_config)

# Compare remote and local
diff <(ssh server cat /etc/config) /etc/config

# Multiple transformations
grep pattern <(sort <(uniq file))
```

---

## Parameter Expansion

Extended forms beyond POSIX.

### Case Modification

```bash
str="hello world"

# First character uppercase
echo "${str^}"        # Hello world

# All uppercase  
echo "${str^^}"       # HELLO WORLD

# First character lowercase
str="HELLO WORLD"
echo "${str,}"        # hELLO WORLD

# All lowercase
echo "${str,,}"       # hello world

# Swap case (with pattern)
echo "${str~}"        # First char
echo "${str~~}"       # All chars
```

### Substring Operations

```bash
str="Hello World"

# Substring: ${var:offset:length}
echo "${str:0:5}"     # Hello
echo "${str:6}"       # World
echo "${str:6:3}"     # Wor

# Negative offset (from end)
echo "${str: -5}"     # World (note space before -)
echo "${str: -5:3}"   # Wor

# Negative length (leave N from end)
echo "${str:0:-3}"    # Hello Wo
```

### Pattern Substitution

```bash
path="/home/user/file.txt"

# Replace first match
echo "${path/user/admin}"     # /home/admin/file.txt

# Replace all matches
echo "${path//\//\\}"         # \home\user\file.txt

# Replace at beginning
echo "${path/#\/home/~}"      # ~/user/file.txt

# Replace at end
echo "${path/%.txt/.md}"      # /home/user/file.md

# Delete (replace with nothing)
echo "${path//\//}"           # homeuserfile.txt
```

### Removal Operations

```bash
path="/home/user/documents/file.txt"

# Remove shortest prefix match
echo "${path#*/}"       # home/user/documents/file.txt

# Remove longest prefix match  
echo "${path##*/}"      # file.txt

# Remove shortest suffix match
echo "${path%/*}"       # /home/user/documents

# Remove longest suffix match
echo "${path%%/*}"      # (empty - removes everything after first /)
```

### Indirect Expansion

```bash
# ${!var} - Value of variable named by var
name="PATH"
echo "${!name}"         # Contents of $PATH

# Works with array indices
arr=(a b c)
i=1
echo "${arr[$i]}"       # b

# All variables with prefix
echo "${!BASH*}"        # BASH BASHPID BASH_VERSION ...
```

### Transformation

```bash
var="hello"

# Quote for reuse
echo "${var@Q}"         # 'hello'

# Escape sequences
var=$'line1\nline2'
echo "${var@E}"         # Expanded escapes

# Prompt expansion
PS1='$ '
echo "${PS1@P}"         # Expanded prompt

# Assignment form
echo "${var@A}"         # var='hello'

# Attributes (for declared vars)
declare -i num=42
echo "${num@a}"         # i (integer attribute)
```

### Default Values

```bash
# Use default if unset or empty
echo "${var:-default}"

# Use default only if unset
echo "${var-default}"

# Assign default if unset or empty
echo "${var:=default}"

# Assign default only if unset
echo "${var=default}"

# Error if unset or empty
echo "${var:?error message}"

# Use alternate value if set
echo "${var:+alternate}"
```

---

## Extended Globbing

Enable with `shopt -s extglob`.

### Patterns

| Pattern | Meaning |
|---------|---------|
| `?(pattern)` | Zero or one match |
| `*(pattern)` | Zero or more matches |
| `+(pattern)` | One or more matches |
| `@(pattern)` | Exactly one match |
| `!(pattern)` | Anything except pattern |

### Examples

```bash
shopt -s extglob

# Match .c or .h files
ls *.@(c|h)

# Match one or more digits
[[ $str == +([0-9]) ]]

# Match optional prefix
ls ?(lib)*.so

# Match except backup files
ls !(*.bak|*.tmp)

# Match repeated pattern
[[ $str == *([a-z]) ]]  # Zero or more lowercase letters

# Complex patterns
ls !(*.o|*.a|.git*)     # Exclude compiled and git files
```

### Pattern Operators in Different Contexts

```bash
# File matching
rm !(important).txt     # Remove all .txt except important.txt

# Case statements
case "$file" in
    *.@(jpg|jpeg|png|gif)) echo "Image" ;;
    *.@(mp3|wav|flac))     echo "Audio" ;;
    *)                      echo "Other" ;;
esac

# Extended tests
if [[ $version == +([0-9]).+([0-9]).+([0-9]) ]]; then
    echo "Valid semver"
fi
```

---

## Glob Qualifiers

Filter glob results by file attributes. Available in Zsh and Lush modes.

### Basic Qualifiers

```bash
# Regular files only
ls *(.)

# Directories only
ls *(/)

# Symbolic links only
ls *(@)

# Executable files only
ls *(*)

# Readable files
ls *(r)

# Writable files
ls *(w)
```

### Combining Qualifiers

```bash
# Executable regular files
ls *(.*)

# Readable directories
ls *(r/)
```

### Common Use Cases

```bash
# List only directories
for dir in */; do
    echo "$dir"
done

# Find executables
ls -la *(*)

# Find broken symlinks
ls *(@-) 2>/dev/null
```

---

## Control Flow Extensions

### Case Fall-Through

```bash
# ;& - Fall through to next pattern
case "$1" in
    -v|--verbose)
        verbose=1
        ;&              # Continue to next
    -d|--debug)
        debug=1
        ;;
esac
# -v sets both verbose and debug

# ;;& - Continue testing patterns
case "$file" in
    *.tar.gz)
        echo "Compressed tarball"
        ;;&             # Keep testing
    *.tar*)
        echo "Tarball"
        ;;&
    *.gz)
        echo "Gzip file"
        ;;
esac
# *.tar.gz matches all three!
```

### Select Loop

Interactive menu:

```bash
PS3="Choose an option: "
select choice in "Option 1" "Option 2" "Option 3" "Quit"; do
    case $choice in
        "Option 1")
            echo "You chose option 1"
            ;;
        "Option 2")
            echo "You chose option 2"
            ;;
        "Option 3")
            echo "You chose option 3"
            ;;
        "Quit")
            break
            ;;
        *)
            echo "Invalid option"
            ;;
    esac
done
```

Output:
```
1) Option 1
2) Option 2
3) Option 3
4) Quit
Choose an option: 
```

### Time Keyword

Measure execution time:

```bash
# Time a command
time sleep 2

# Time a pipeline
time (find . -name "*.c" | xargs grep pattern)

# Time a block
time {
    cmd1
    cmd2
    cmd3
}

# Output format (example)
# real    0m2.003s
# user    0m0.001s
# sys     0m0.001s
```

---

## Functions

### Standard Functions

```bash
# Declaration
my_function() {
    local arg1="$1"
    local arg2="$2"
    echo "Arguments: $arg1, $arg2"
    return 0
}

# function keyword (optional)
function another_function {
    echo "Also valid"
}

# Call
my_function "hello" "world"
```

### Local Variables

```bash
my_func() {
    local x=10          # Local to function
    local -i num=42     # Local integer
    local -a arr=(1 2)  # Local array
    local -A map        # Local associative array
}
```

### Nameref Variables

Pass by reference:

```bash
# local -n creates a nameref
swap() {
    local -n ref1=$1
    local -n ref2=$2
    local tmp=$ref1
    ref1=$ref2
    ref2=$tmp
}

a=1
b=2
swap a b
echo "$a $b"    # 2 1

# Useful for returning values
get_value() {
    local -n result=$1
    result="computed value"
}

get_value myvar
echo "$myvar"   # computed value
```

### Anonymous Functions

Immediate execution:

```bash
# Execute immediately
() {
    local temp="local scope"
    echo "$temp"
}

# With arguments
() {
    echo "Args: $@"
} arg1 arg2
```

---

## Mode Requirements

| Feature | POSIX | Bash | Zsh | Lush |
|---------|-------|------|-----|--------|
| Indexed arrays | No | Yes | Yes | Yes |
| Associative arrays | No | Yes | Yes | Yes |
| Arithmetic `(())` | No | Yes | Yes | Yes |
| Extended test `[[]]` | No | Yes | Yes | Yes |
| Process substitution | No | Yes | Yes | Yes |
| Case modification | No | Yes | Yes | Yes |
| Extended globbing | No | Yes | Yes | Yes |
| Glob qualifiers | No | No | Yes | Yes |
| `;&` fall-through | No | Yes | Yes | Yes |
| `select` loop | No | Yes | Yes | Yes |
| `time` keyword | No | Yes | Yes | Yes |
| Nameref `local -n` | No | Yes | Yes | Yes |
| Anonymous functions | No | No | Yes | Yes |

To check your current mode:

```bash
set -o | grep -E "posix|bash|zsh|lush"
```

To change mode:

```bash
set -o lush    # Default, all features
set -o bash      # Bash compatibility
set -o zsh       # Zsh compatibility
set -o posix     # Strict POSIX
```

---

## See Also

- [USER_GUIDE.md](USER_GUIDE.md) - Complete feature overview
- [CONFIGURATION.md](CONFIGURATION.md) - Mode documentation
- [ADVANCED_SCRIPTING_GUIDE.md](ADVANCED_SCRIPTING_GUIDE.md) - Scripting techniques
