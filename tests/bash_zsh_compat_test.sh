#!/bin/bash

# =============================================================================
# BASH/ZSH COMPATIBILITY FEATURES TEST SUITE
# =============================================================================
#
# This test suite validates shell compatibility features and lush's shell
# mode system. It is designed to:
#
# 1. PASS 100% on bash (for bash-specific features)
# 2. PASS 100% on zsh (for zsh-specific features)
# 3. Test lush in different modes:
#    - lush (default): Curated best-of-both, tests shared features
#    - lush --bash:    Should pass 100% of bash-specific tests
#    - lush --zsh:     Should pass 100% of zsh-specific tests
#    - lush --posix:   Strict POSIX mode (separate test suite)
#
# Usage:
#   ./bash_zsh_compat_test.sh [shell_path] [--mode=bash|zsh|lush|posix]
#
# Examples:
#   ./bash_zsh_compat_test.sh /bin/bash           # Test bash baseline
#   ./bash_zsh_compat_test.sh /bin/zsh            # Test zsh baseline
#   ./bash_zsh_compat_test.sh ./build/lush      # Test lush default mode
#   ./bash_zsh_compat_test.sh ./build/lush --mode=bash  # Test lush in bash mode
#   ./bash_zsh_compat_test.sh ./build/lush --mode=zsh   # Test lush in zsh mode
#
# Key differences between bash and zsh:
# - Arrays: bash is 0-indexed, zsh is 1-indexed
# - Options: bash uses shopt, zsh uses setopt
# - Case mod: bash uses ${var^^}, zsh uses ${(U)var}
# - Namerefs: bash uses declare -n, zsh uses different mechanism
# - mapfile: bash builtin, zsh doesn't have it
# - Extended glob: bash ?()/*()/+()/!(), zsh #/~/^ patterns
#
# =============================================================================

set -uo pipefail

# Get absolute path to shell binary to test
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TEST_SHELL="${1:-$PROJECT_DIR/build/lush}"
TEST_MODE=""  # Empty = auto-detect, or bash/zsh/lush/posix
TEST_DIR="/tmp/lush_compat_test_$$"
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        --mode=*)
            TEST_MODE="${arg#--mode=}"
            ;;
    esac
done

# Track failures for summary
declare -a FAILURE_DETAILS=()

# Detect which shell we're testing
SHELL_NAME=$(basename "$TEST_SHELL")
IS_BASH=0
IS_ZSH=0
IS_LUSH=0
IS_LUSH_BINARY=0  # True if the actual binary is lush (for option syntax)
LUSH_MODE=""  # bash, zsh, lush, or posix when testing lush

case "$SHELL_NAME" in
    bash*) IS_BASH=1 ;;
    zsh*) IS_ZSH=1 ;;
    lush*)
        IS_LUSH=1
        IS_LUSH_BINARY=1  # Remember this is lush binary
        # If a mode was specified, lush will emulate that shell
        if [[ -n "$TEST_MODE" ]]; then
            LUSH_MODE="$TEST_MODE"
            case "$TEST_MODE" in
                bash)
                    # In bash mode, lush should behave like bash
                    IS_BASH=1
                    IS_LUSH=0  # Treat as bash for test selection
                    ;;
                zsh)
                    # In zsh mode, lush should behave like zsh
                    IS_ZSH=1
                    IS_LUSH=0  # Treat as zsh for test selection
                    ;;
                posix)
                    # POSIX mode - very restricted
                    ;;
                lush)
                    # Default lush mode
                    ;;
            esac
        fi
        ;;
esac

# Mode prefix to add to scripts when testing lush in a specific mode
# Uses the `mode` builtin (lush-native canonical) rather than the
# bash-bridge `set -o posix`, so all four mode names work uniformly.
MODE_PREFIX=""
if [[ -n "$LUSH_MODE" ]]; then
    MODE_PREFIX="mode $LUSH_MODE"$'\n'
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
NC='\033[0m'

# Cleanup on exit
cleanup() {
    cd /
    rm -rf "$TEST_DIR" 2>/dev/null || true
}
trap cleanup EXIT

# Setup test directory
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

print_header() {
    echo -e "\n${BLUE}===============================================================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}===============================================================================${NC}\n"
}

print_section() {
    echo -e "\n${CYAN}=== $1 ===${NC}"
}

# Record test result
test_result() {
    local test_name="$1"
    local result="$2"  # 0=pass, 1=fail, 2=skip
    local details="${3:-}"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    if [[ $result -eq 0 ]]; then
        echo -e "  ${GREEN}✓${NC} $test_name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    elif [[ $result -eq 2 ]]; then
        echo -e "  ${YELLOW}○${NC} $test_name ${YELLOW}[SKIPPED - not applicable to $SHELL_NAME]${NC}"
        SKIPPED_TESTS=$((SKIPPED_TESTS + 1))
    else
        echo -e "  ${RED}✗${NC} $test_name"
        [[ -n "$details" ]] && echo -e "    ${YELLOW}$details${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        FAILURE_DETAILS+=("$test_name: $details")
    fi
}

# Run a script file and check EXACT output match
run_script_test() {
    local test_name="$1"
    local script_content="$2"
    local expected="$3"

    local script_file="$TEST_DIR/test_$$.sh"
    # Add mode prefix for lush mode testing
    echo "${MODE_PREFIX}${script_content}" > "$script_file"
    chmod +x "$script_file"

    local output
    output=$("$TEST_SHELL" "$script_file" 2>&1) || true
    rm -f "$script_file"

    # Trim trailing whitespace
    output=$(echo "$output" | sed 's/[[:space:]]*$//')
    expected=$(echo "$expected" | sed 's/[[:space:]]*$//')

    if [[ "$output" == "$expected" ]]; then
        test_result "$test_name" 0
    else
        test_result "$test_name" 1 "Expected: '$expected', Got: '$output'"
    fi
}

# Run script, sort output (for globs), check match
run_script_sorted() {
    local test_name="$1"
    local script_content="$2"
    local expected="$3"

    local script_file="$TEST_DIR/test_$$.sh"
    # Add mode prefix for lush mode testing
    echo "${MODE_PREFIX}${script_content}" > "$script_file"
    chmod +x "$script_file"

    local output
    output=$("$TEST_SHELL" "$script_file" 2>&1) || true
    rm -f "$script_file"

    # Sort both for comparison
    output=$(echo "$output" | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/[[:space:]]*$//')
    expected=$(echo "$expected" | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/[[:space:]]*$//')

    if [[ "$output" == "$expected" ]]; then
        test_result "$test_name" 0
    else
        test_result "$test_name" 1 "Expected (sorted): '$expected', Got (sorted): '$output'"
    fi
}

# Run script and check output contains expected
run_script_contains() {
    local test_name="$1"
    local script_content="$2"
    local expected="$3"

    local script_file="$TEST_DIR/test_$$.sh"
    # Add mode prefix for lush mode testing
    echo "${MODE_PREFIX}${script_content}" > "$script_file"
    chmod +x "$script_file"

    local output
    output=$("$TEST_SHELL" "$script_file" 2>&1) || true
    rm -f "$script_file"

    if [[ "$output" == *"$expected"* ]]; then
        test_result "$test_name" 0
    else
        test_result "$test_name" 1 "Should contain: '$expected', Got: '$output'"
    fi
}

# Skip test (not applicable to current shell)
skip_test() {
    local test_name="$1"
    test_result "$test_name" 2
}

# =============================================================================
print_header "BASH/ZSH COMPATIBILITY TEST SUITE"
# =============================================================================

echo "Testing shell: $TEST_SHELL"
if [[ -n "$LUSH_MODE" ]]; then
    echo "Shell type: $SHELL_NAME in '$LUSH_MODE' mode (emulating: bash=$IS_BASH, zsh=$IS_ZSH)"
else
    echo "Shell type: $SHELL_NAME (bash=$IS_BASH, zsh=$IS_ZSH, lush=$IS_LUSH)"
fi
echo "Test directory: $TEST_DIR"
echo "Date: $(date -Iseconds)"

# Verify shell exists
if [[ ! -x "$TEST_SHELL" ]]; then
    echo -e "${RED}ERROR: Shell not found at $TEST_SHELL${NC}"
    exit 1
fi

# =============================================================================
print_section "1. ANSI-C QUOTING (\$'...')"
# Both bash and zsh support this identically
# =============================================================================

run_script_test "ANSI-C: newline \\n" \
    'echo $'"'"'hello\nworld'"'"'' \
    $'hello\nworld'

run_script_test "ANSI-C: tab \\t" \
    'echo $'"'"'hello\tworld'"'"'' \
    $'hello\tworld'

run_script_test "ANSI-C: backslash \\\\" \
    'echo $'"'"'hello\\world'"'"'' \
    'hello\world'

run_script_test "ANSI-C: hex \\x41 = A" \
    'echo $'"'"'\x41\x42\x43'"'"'' \
    'ABC'

run_script_test "ANSI-C: unicode \\u0041 = A" \
    'echo $'"'"'\u0041'"'"'' \
    'A'

run_script_test "ANSI-C: unicode \\u00e9 = é" \
    'echo $'"'"'\u00e9'"'"'' \
    'é'

# =============================================================================
print_section "2. BRACE EXPANSION"
# Both bash and zsh support this identically
# =============================================================================

run_script_test "brace: {1..5}" \
    'echo {1..5}' \
    '1 2 3 4 5'

run_script_test "brace: {a..e}" \
    'echo {a..e}' \
    'a b c d e'

run_script_test "brace: {5..1} reverse" \
    'echo {5..1}' \
    '5 4 3 2 1'

run_script_test "brace: {1..10..2} step" \
    'echo {1..10..2}' \
    '1 3 5 7 9'

run_script_test "brace: {01..05} zero-padded" \
    'echo {01..05}' \
    '01 02 03 04 05'

run_script_test "brace: {a,b,c} comma list" \
    'echo {a,b,c}' \
    'a b c'

run_script_test "brace: file{1..3}.txt prefix+suffix" \
    'echo file{1..3}.txt' \
    'file1.txt file2.txt file3.txt'

run_script_test "brace: pre{a,b,c}fix" \
    'echo pre{a,b,c}fix' \
    'preafix prebfix precfix'

run_script_test "brace: {a,b,c}_suffix suffix-only" \
    'echo {a,b,c}_suffix' \
    'a_suffix b_suffix c_suffix'

run_script_test "brace: {1..2}{a..b} cartesian" \
    'echo {1..2}{a..b}' \
    '1a 1b 2a 2b'

# =============================================================================
print_section "3. PUSHD/POPD/DIRS"
# Both bash and zsh support this identically
# =============================================================================

run_script_test "pushd: changes directory" \
    'cd /tmp; pushd /usr >/dev/null; pwd' \
    '/usr'

run_script_test "popd: returns to previous" \
    'cd /tmp; pushd /usr >/dev/null; popd >/dev/null; pwd' \
    '/tmp'

run_script_test "dirs: shows stack" \
    'cd /; pushd /tmp >/dev/null; pushd /usr >/dev/null; dirs' \
    '/usr /tmp /'

run_script_contains "popd: empty stack error" \
    'cd /tmp; popd 2>&1' \
    'stack'

# =============================================================================
print_section "4. MAPFILE/READARRAY (Bash-specific)"
# zsh does not have mapfile - uses different mechanisms
# =============================================================================

# Create test file
echo -e "line1\nline2\nline3\nline4\nline5" > "$TEST_DIR/testfile.txt"

if [[ $IS_ZSH -eq 1 ]]; then
    skip_test "mapfile: count lines"
    skip_test "mapfile: first element"
    skip_test "mapfile -n: limit count"
    skip_test "mapfile -s: skip lines"
    skip_test "readarray: alias works"
else
    run_script_test "mapfile: count lines" \
        'mapfile -t arr < '"$TEST_DIR"'/testfile.txt; echo "${#arr[@]}"' \
        '5'

    run_script_test "mapfile: first element" \
        'mapfile -t arr < '"$TEST_DIR"'/testfile.txt; echo "${arr[0]}"' \
        'line1'

    run_script_test "mapfile -n: limit count" \
        'mapfile -t -n 2 arr < '"$TEST_DIR"'/testfile.txt; echo "${#arr[@]}"' \
        '2'

    run_script_test "mapfile -s: skip lines" \
        'mapfile -t -s 2 arr < '"$TEST_DIR"'/testfile.txt; echo "${arr[0]}"' \
        'line3'

    run_script_test "readarray: alias works" \
        'readarray -t arr < '"$TEST_DIR"'/testfile.txt; echo "${arr[2]}"' \
        'line3'
fi

# =============================================================================
print_section "5. NAMEREF VARIABLES (Bash-specific declare -n)"
# zsh uses typeset -n but with different semantics
# =============================================================================

if [[ $IS_ZSH -eq 1 ]]; then
    skip_test "nameref: basic read"
    skip_test "nameref: modify through ref"
    skip_test "nameref: local -n in function"
    skip_test "nameref: chain resolution"
else
    run_script_test "nameref: basic read" \
        'x=hello; declare -n ref=x; echo "$ref"' \
        'hello'

    run_script_test "nameref: modify through ref" \
        'x=hello; declare -n ref=x; ref=world; echo "$x"' \
        'world'

    run_script_test "nameref: local -n in function" \
        'modify() { local -n ref=$1; ref=modified; }; var=original; modify var; echo "$var"' \
        'modified'

    run_script_test "nameref: chain resolution" \
        'a=value; declare -n b=a; declare -n c=b; echo "$c"' \
        'value'
fi

# =============================================================================
print_section "6. EXTENDED GLOB - BASH STYLE"
# Bash: uses shopt -s extglob with ?()/*()/+()/@()/!() syntax
# Lush: has extglob on by default, uses bash syntax
# =============================================================================

# Create test files
mkdir -p "$TEST_DIR/globtest"
touch "$TEST_DIR/globtest/file.txt"
touch "$TEST_DIR/globtest/file1.txt"
touch "$TEST_DIR/globtest/file12.txt"
touch "$TEST_DIR/globtest/a.txt"
touch "$TEST_DIR/globtest/aa.txt"
touch "$TEST_DIR/globtest/aaa.txt"
touch "$TEST_DIR/globtest/cat.txt"
touch "$TEST_DIR/globtest/dog.txt"
touch "$TEST_DIR/globtest/bird.txt"

if [[ $IS_ZSH -eq 1 ]]; then
    # Zsh doesn't use bash-style extglob - skip these
    skip_test "bash-extglob: ?(pattern) zero-or-one"
    skip_test "bash-extglob: *(pattern) zero-or-more"
    skip_test "bash-extglob: +(pattern) one-or-more"
    skip_test "bash-extglob: @(pat|pat) alternation"
    skip_test "bash-extglob: !(pattern) negation"
else
    # Bash and lush use same extglob syntax, but different option commands
    # Note: IS_LUSH_BINARY tracks if we're running lush binary (even in bash mode)
    if [[ $IS_LUSH_BINARY -eq 1 ]]; then
        # lush uses setopt; in bash mode extglob is OFF by default (like real bash)
        # so we need to enable it explicitly
        if [[ -n "$LUSH_MODE" ]]; then
            EXTGLOB_ENABLE="setopt extended_glob"
        else
            EXTGLOB_ENABLE=""  # lush default mode has extglob on
        fi
    else
        EXTGLOB_ENABLE="shopt -s extglob"
    fi

    run_script_sorted "bash-extglob: ?(pattern) zero-or-one" \
        "$EXTGLOB_ENABLE"'
cd '"$TEST_DIR"'/globtest
echo file?([0-9]).txt' \
        'file.txt file1.txt'

    run_script_sorted "bash-extglob: *(pattern) zero-or-more" \
        "$EXTGLOB_ENABLE"'
cd '"$TEST_DIR"'/globtest
echo file*([0-9]).txt' \
        'file.txt file1.txt file12.txt'

    run_script_sorted "bash-extglob: +(pattern) one-or-more" \
        "$EXTGLOB_ENABLE"'
cd '"$TEST_DIR"'/globtest
echo +([a]).txt' \
        'a.txt aa.txt aaa.txt'

    run_script_sorted "bash-extglob: @(pat|pat) alternation" \
        "$EXTGLOB_ENABLE"'
cd '"$TEST_DIR"'/globtest
echo @(cat|dog).txt' \
        'cat.txt dog.txt'

    run_script_sorted "bash-extglob: !(pattern) negation" \
        "$EXTGLOB_ENABLE"'
cd '"$TEST_DIR"'/globtest
echo !(file*).txt' \
        'a.txt aa.txt aaa.txt bird.txt cat.txt dog.txt'
fi

# =============================================================================
print_section "7. EXTENDED GLOB - ZSH STYLE"
# Zsh: uses setopt extended_glob with #/~/^ patterns
# =============================================================================

if [[ $IS_ZSH -eq 1 ]]; then
    # Zsh extended glob syntax tests
    # # = zero or more of preceding (like * in regex)
    # ## = one or more of preceding (like + in regex)
    # ~ = exclusion (like !(pattern))
    # ^ = negation/toggle

    # In zsh extended glob: # = zero or more, ## = one or more
    # file[0-9]#.txt matches file.txt, file1.txt, file12.txt (zero or more digits)
    run_script_sorted "zsh-extglob: [0-9]# zero-or-more digits" \
        'setopt extended_glob
cd '"$TEST_DIR"'/globtest
echo file[0-9]#.txt' \
        'file.txt file1.txt file12.txt'

    run_script_sorted "zsh-extglob: a## one-or-more" \
        'setopt extended_glob
cd '"$TEST_DIR"'/globtest
echo a##.txt' \
        'a.txt aa.txt aaa.txt'

    run_script_sorted "zsh-extglob: (cat|dog) alternation" \
        'setopt extended_glob
cd '"$TEST_DIR"'/globtest
echo (cat|dog).txt' \
        'cat.txt dog.txt'

    run_script_sorted "zsh-extglob: ^file* negation" \
        'setopt extended_glob null_glob
cd '"$TEST_DIR"'/globtest
echo ^file*.txt' \
        'a.txt aa.txt aaa.txt bird.txt cat.txt dog.txt'
else
    skip_test "zsh-extglob: a# zero-or-more"
    skip_test "zsh-extglob: a## one-or-more"
    skip_test "zsh-extglob: (cat|dog) alternation"
    skip_test "zsh-extglob: ^file* negation"
fi

# =============================================================================
print_section "8. NULLGLOB"
# Bash: shopt -s nullglob
# Zsh: setopt null_glob (or NULL_GLOB)
# Lush: nullglob on by default
# =============================================================================

if [[ $IS_LUSH_BINARY -eq 1 ]]; then
    # Lush uses setopt; in bash/zsh mode nullglob follows that shell's default
    if [[ -n "$LUSH_MODE" && "$LUSH_MODE" != "lush" ]]; then
        # In bash/zsh mode, need to enable nullglob explicitly
        run_script_test "nullglob: non-match returns empty" \
            'setopt null_glob
echo nonexistent*.xyz' \
            ''
    else
        # Lush default mode has nullglob on
        run_script_test "nullglob: non-match returns empty" \
            'echo nonexistent*.xyz' \
            ''
    fi
elif [[ $IS_ZSH -eq 1 ]]; then
    run_script_test "nullglob: non-match returns empty" \
        'setopt null_glob
echo nonexistent*.xyz' \
        ''
else
    run_script_test "nullglob: non-match returns empty" \
        'shopt -s nullglob
echo nonexistent*.xyz' \
        ''
fi

# =============================================================================
print_section "9. CASE MODIFICATION - BASH SYNTAX"
# Bash: ${var^^}, ${var,,}, ${var^}, ${var,}
# =============================================================================

if [[ $IS_ZSH -eq 1 ]]; then
    skip_test "bash-case: \${var^^} uppercase"
    skip_test "bash-case: \${var,,} lowercase"
    skip_test "bash-case: \${var^} first char upper"
    skip_test "bash-case: \${var,} first char lower"
    skip_test "bash-case: unicode uppercase"
else
    run_script_test "bash-case: \${var^^} uppercase" \
        'x=hello; echo "${x^^}"' \
        'HELLO'

    run_script_test "bash-case: \${var,,} lowercase" \
        'x=HELLO; echo "${x,,}"' \
        'hello'

    run_script_test "bash-case: \${var^} first char upper" \
        'x=hello; echo "${x^}"' \
        'Hello'

    run_script_test "bash-case: \${var,} first char lower" \
        'x=HELLO; echo "${x,}"' \
        'hELLO'

    run_script_test "bash-case: unicode uppercase" \
        'x=café; echo "${x^^}"' \
        'CAFÉ'
fi

# =============================================================================
print_section "10. CASE MODIFICATION - ZSH SYNTAX"
# Zsh: ${(U)var}, ${(L)var}
# =============================================================================

if [[ $IS_ZSH -eq 1 ]]; then
    run_script_test "zsh-case: \${(U)var} uppercase" \
        'x=hello; echo "${(U)x}"' \
        'HELLO'

    run_script_test "zsh-case: \${(L)var} lowercase" \
        'x=HELLO; echo "${(L)x}"' \
        'hello'

    run_script_test "zsh-case: \${(C)var} capitalize words" \
        'x="hello world"; echo "${(C)x}"' \
        'Hello World'

    run_script_test "zsh-case: unicode uppercase" \
        'x=café; echo "${(U)x}"' \
        'CAFÉ'
else
    skip_test "zsh-case: \${(U)var} uppercase"
    skip_test "zsh-case: \${(L)var} lowercase"
    skip_test "zsh-case: \${(C)var} capitalize words"
    skip_test "zsh-case: unicode uppercase"
fi

# =============================================================================
print_section "11. ZSH PARAMETER EXPANSION FLAGS"
# Zsh-specific: ${(f)var}, ${(j:,:)arr}, ${(s:,:)var}, etc.
# =============================================================================

if [[ $IS_ZSH -eq 1 ]]; then
    run_script_test "zsh-flags: \${(f)var} split on newlines" \
        'x=$'"'"'line1\nline2\nline3'"'"'
arr=("${(f)x}")
echo "${#arr[@]}"' \
        '3'

    run_script_test "zsh-flags: \${(j:,:)arr} join with comma" \
        'arr=(a b c d)
echo "${(j:,:)arr}"' \
        'a,b,c,d'

    run_script_test "zsh-flags: \${(s:,:)var} split on comma" \
        'x=a,b,c,d
arr=("${(s:,:)x}")
echo "${#arr[@]}"' \
        '4'

    run_script_test "zsh-flags: \${(o)arr} sort" \
        'arr=(delta alpha charlie bravo)
echo "${(o)arr[@]}"' \
        'alpha bravo charlie delta'

    run_script_test "zsh-flags: \${(O)arr} reverse sort" \
        'arr=(delta alpha charlie bravo)
echo "${(O)arr[@]}"' \
        'delta charlie bravo alpha'

    run_script_test "zsh-flags: \${#var} length" \
        'x=hello; echo "${#x}"' \
        '5'

    run_script_test "zsh-flags: \${(w)#var} word count" \
        'x="one two three four"; echo "${(w)#x}"' \
        '4'
else
    skip_test "zsh-flags: \${(f)var} split on newlines"
    skip_test "zsh-flags: \${(j:,:)arr} join with comma"
    skip_test "zsh-flags: \${(s:,:)var} split on comma"
    skip_test "zsh-flags: \${(o)arr} sort"
    skip_test "zsh-flags: \${(O)arr} reverse sort"
    skip_test "zsh-flags: \${#var} length"
    skip_test "zsh-flags: \${(w)#var} word count"
fi

# =============================================================================
print_section "12. ZSH GLOB QUALIFIERS"
# Zsh-specific: *(.) files only, *(/) dirs only, *(@) symlinks, etc.
# =============================================================================

# Setup test environment for glob qualifiers
mkdir -p "$TEST_DIR/qualtest/subdir"
touch "$TEST_DIR/qualtest/file1.txt"
touch "$TEST_DIR/qualtest/file2.txt"
ln -sf file1.txt "$TEST_DIR/qualtest/link.txt"

if [[ $IS_ZSH -eq 1 ]]; then
    run_script_sorted "zsh-glob-qual: *(.) regular files only" \
        'cd '"$TEST_DIR"'/qualtest
echo *(.)' \
        'file1.txt file2.txt'

    run_script_test "zsh-glob-qual: *(/) directories only" \
        'cd '"$TEST_DIR"'/qualtest
echo *(/)' \
        'subdir'

    run_script_test "zsh-glob-qual: *(@) symlinks only" \
        'cd '"$TEST_DIR"'/qualtest
echo *(@)' \
        'link.txt'

    run_script_sorted "zsh-glob-qual: *(.,@) files and symlinks" \
        'cd '"$TEST_DIR"'/qualtest
echo *(.,@)' \
        'file1.txt file2.txt link.txt'
else
    skip_test "zsh-glob-qual: *(.) regular files only"
    skip_test "zsh-glob-qual: *(/) directories only"
    skip_test "zsh-glob-qual: *(@) symlinks only"
    skip_test "zsh-glob-qual: *(.,@) files and symlinks"
fi

# =============================================================================
print_section "13. ARRAY OPERATIONS - BASH (0-indexed)"
# Bash: 0-indexed arrays
# =============================================================================

if [[ $IS_ZSH -eq 1 ]]; then
    skip_test "bash-array: 0-indexed access"
    skip_test "bash-array: all elements \${arr[@]}"
    skip_test "bash-array: length \${#arr[@]}"
    skip_test "bash-array: element assignment arr[n]=value"
    skip_test "bash-array: append element"
else
    run_script_test "bash-array: 0-indexed access" \
        'arr=(one two three); echo "${arr[1]}"' \
        'two'

    run_script_test "bash-array: all elements \${arr[@]}" \
        'arr=(a b c); echo "${arr[@]}"' \
        'a b c'

    run_script_test "bash-array: length \${#arr[@]}" \
        'arr=(1 2 3 4 5); echo "${#arr[@]}"' \
        '5'

    run_script_test "bash-array: element assignment arr[n]=value" \
        'arr=(one two three); arr[1]=TWO; echo "${arr[1]}"' \
        'TWO'

    run_script_test "bash-array: append element" \
        'arr=(a b c); arr[3]=d; echo "${arr[@]}"' \
        'a b c d'
fi

# =============================================================================
print_section "14. ARRAY OPERATIONS - ZSH (1-indexed)"
# Zsh: 1-indexed arrays by default
# =============================================================================

if [[ $IS_ZSH -eq 1 ]]; then
    run_script_test "zsh-array: 1-indexed access" \
        'arr=(one two three); echo "${arr[2]}"' \
        'two'

    run_script_test "zsh-array: all elements \${arr[@]}" \
        'arr=(a b c); echo "${arr[@]}"' \
        'a b c'

    run_script_test "zsh-array: length \${#arr[@]}" \
        'arr=(1 2 3 4 5); echo "${#arr[@]}"' \
        '5'

    run_script_test "zsh-array: element assignment arr[n]=value" \
        'arr=(one two three); arr[2]=TWO; echo "${arr[2]}"' \
        'TWO'

    run_script_test "zsh-array: append with +=" \
        'arr=(a b c); arr+=d; echo "${arr[@]}"' \
        'a b c d'
else
    skip_test "zsh-array: 1-indexed access"
    skip_test "zsh-array: all elements \${arr[@]}"
    skip_test "zsh-array: length \${#arr[@]}"
    skip_test "zsh-array: element assignment arr[n]=value"
    skip_test "zsh-array: append with +="
fi

# =============================================================================
print_section "15. ASSOCIATIVE ARRAYS"
# Both bash and zsh support these (with minor differences)
# =============================================================================

if [[ $IS_ZSH -eq 1 ]]; then
    run_script_test "assoc-array: declare and set" \
        'typeset -A arr; arr[key]=value; echo "${arr[key]}"' \
        'value'

    run_script_test "assoc-array: multiple keys" \
        'typeset -A arr; arr=(k1 v1 k2 v2); echo "${arr[k1]} ${arr[k2]}"' \
        'v1 v2'

    run_script_sorted "assoc-array: get all keys \${(k)arr}" \
        'typeset -A arr; arr=(a 1 b 2 c 3); echo "${(k)arr[@]}"' \
        'a b c'

    run_script_sorted "assoc-array: get all values \${(v)arr}" \
        'typeset -A arr; arr=(a 1 b 2 c 3); echo "${(v)arr[@]}"' \
        '1 2 3'
else
    run_script_test "assoc-array: declare and set" \
        'declare -A arr; arr[key]=value; echo "${arr[key]}"' \
        'value'

    run_script_test "assoc-array: multiple keys" \
        'declare -A arr; arr=([k1]=v1 [k2]=v2); echo "${arr[k1]} ${arr[k2]}"' \
        'v1 v2'

    run_script_sorted "assoc-array: get all keys \${!arr[@]}" \
        'declare -A arr; arr=([a]=1 [b]=2 [c]=3); echo "${!arr[@]}"' \
        'a b c'

    run_script_sorted "assoc-array: get all values \${arr[@]}" \
        'declare -A arr; arr=([a]=1 [b]=2 [c]=3); echo "${arr[@]}"' \
        '1 2 3'
fi

# =============================================================================
print_section "16. PROCESS SUBSTITUTION"
# Both bash and zsh support <(cmd) and >(cmd)
# =============================================================================

run_script_test "process-subst: <(cmd) read from process" \
    'cat <(echo "hello")' \
    'hello'

run_script_test "process-subst: paste from two processes" \
    'paste <(echo -e "a\nb\nc") <(echo -e "1\n2\n3")' \
    $'a\t1\nb\t2\nc\t3'

# =============================================================================
print_section "17. ARITHMETIC EXPANSION"
# Both bash and zsh support $(( )) and (( ))
# =============================================================================

run_script_test "arith: basic addition" \
    'echo $((2 + 3))' \
    '5'

run_script_test "arith: multiplication" \
    'echo $((4 * 5))' \
    '20'

run_script_test "arith: variable in expression" \
    'x=10; echo $((x + 5))' \
    '15'

run_script_test "arith: compound assignment" \
    'x=5; ((x += 3)); echo $x' \
    '8'

run_script_test "arith: comparison returns 0/1" \
    'echo $((5 > 3))' \
    '1'

# =============================================================================
print_section "18. EXTENDED TEST [[ ]]"
# Both bash and zsh support extended test syntax
# =============================================================================

run_script_test "test: string equality" \
    '[[ "hello" == "hello" ]] && echo yes || echo no' \
    'yes'

run_script_test "test: string inequality" \
    '[[ "hello" != "world" ]] && echo yes || echo no' \
    'yes'

run_script_test "test: pattern match ==" \
    '[[ "hello" == hel* ]] && echo yes || echo no' \
    'yes'

run_script_test "test: regex match =~" \
    '[[ "hello123" =~ ^[a-z]+[0-9]+$ ]] && echo yes || echo no' \
    'yes'

run_script_test "test: -z empty string" \
    'x=""; [[ -z "$x" ]] && echo yes || echo no' \
    'yes'

run_script_test "test: -n non-empty string" \
    'x="hello"; [[ -n "$x" ]] && echo yes || echo no' \
    'yes'

run_script_test "test: && and || in [[ ]]" \
    '[[ 1 -eq 1 && 2 -eq 2 ]] && echo yes || echo no' \
    'yes'

# =============================================================================
print_section "SUMMARY"
# =============================================================================

echo ""
echo -e "${BLUE}===============================================================================${NC}"
if [[ -n "$LUSH_MODE" ]]; then
    echo -e "Test Results for: $SHELL_NAME (mode: $LUSH_MODE)"
else
    echo -e "Test Results for: $SHELL_NAME"
fi
echo -e "  Total:            ${TOTAL_TESTS}"
echo -e "  ${GREEN}Passed:           ${PASSED_TESTS}${NC}"
if [[ $SKIPPED_TESTS -gt 0 ]]; then
    if [[ -n "$LUSH_MODE" ]]; then
        echo -e "  ${YELLOW}Skipped:          ${SKIPPED_TESTS}${NC} (not applicable to $LUSH_MODE mode)"
    else
        echo -e "  ${YELLOW}Skipped:          ${SKIPPED_TESTS}${NC} (not applicable to $SHELL_NAME)"
    fi
fi
if [[ $FAILED_TESTS -gt 0 ]]; then
    echo -e "  ${RED}Failed:           ${FAILED_TESTS}${NC}"
fi
echo -e "${BLUE}===============================================================================${NC}"

if [[ $FAILED_TESTS -gt 0 ]]; then
    echo ""
    echo -e "${YELLOW}Failed Tests:${NC}"
    for detail in "${FAILURE_DETAILS[@]}"; do
        echo -e "  - $detail"
    done
    echo ""
fi

# Calculate pass rate (excluding skipped)
APPLICABLE_TESTS=$((TOTAL_TESTS - SKIPPED_TESTS))
if [[ $APPLICABLE_TESTS -gt 0 ]]; then
    PASS_RATE=$((PASSED_TESTS * 100 / APPLICABLE_TESTS))
    echo -e "Pass rate: ${PASS_RATE}% (${PASSED_TESTS}/${APPLICABLE_TESTS} applicable tests)"
fi

if [[ $FAILED_TESTS -gt 0 ]]; then
    echo -e "\n${RED}${FAILED_TESTS} test failure(s)${NC}"
    exit 1
else
    echo -e "\n${GREEN}All applicable tests passed!${NC}"
    exit 0
fi
