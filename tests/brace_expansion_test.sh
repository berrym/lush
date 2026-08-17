#!/bin/bash

# =============================================================================
# BRACE EXPANSION TESTS
# =============================================================================
#
# Tests brace expansion features including:
# - Comma lists: {a,b,c}
# - Numeric ranges: {1..10}
# - Character ranges: {a..z}
# - Reverse ranges: {10..1}, {z..a}
# - Step values: {1..10..2}
# - Zero-padding: {01..05}
# - Prefix/suffix: file{1..3}.txt
# - Nested braces (future)
# - For loop integration
# - Command group distinction: { cmd; }
#
# =============================================================================

set -euo pipefail

# Get absolute path to lush binary
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
LUSH="${1:-$PROJECT_DIR/build/lush}"
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
PURPLE='\033[1;35m'
NC='\033[0m'

print_header() {
    echo -e "\n${BLUE}===============================================================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}===============================================================================${NC}\n"
}

print_section() {
    echo -e "\n${CYAN}=== $1 ===${NC}"
}

test_result() {
    local test_name="$1"
    local result="$2"  # 0 for pass, 1 for fail
    local details="${3:-}"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    if [[ $result -eq 0 ]]; then
        echo -e "  ${GREEN}OK${NC} $test_name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "  ${RED}FAIL${NC} $test_name"
        [[ -n "$details" ]] && echo -e "    ${YELLOW}$details${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

# Helper to run lush command and check output
run_test() {
    local test_name="$1"
    local command="$2"
    local expected="$3"

    local output
    output=$(echo "$command" | "$LUSH" 2>&1) || true
    # Trim trailing whitespace
    output=$(echo "$output" | sed 's/[[:space:]]*$//')
    expected=$(echo "$expected" | sed 's/[[:space:]]*$//')

    if [[ "$output" == "$expected" ]]; then
        test_result "$test_name" 0
    else
        test_result "$test_name" 1 "Expected: '$expected', Got: '$output'"
    fi
}

# =============================================================================
# Main Tests
# =============================================================================

print_header "BRACE EXPANSION TESTS"

# -----------------------------------------------------------------------------
print_section "Comma List Expansion"
# -----------------------------------------------------------------------------

run_test "Simple comma list" \
    "echo {a,b,c}" \
    "a b c"

run_test "Numeric comma list" \
    "echo {1,2,3}" \
    "1 2 3"

run_test "Two items" \
    "echo {foo,bar}" \
    "foo bar"

run_test "Single item (no expansion)" \
    "echo {solo}" \
    "{solo}"

run_test "Empty item in list" \
    "echo {a,,c}" \
    "a  c"

run_test "Comma list with prefix" \
    "echo file{A,B,C}" \
    "fileA fileB fileC"

run_test "Comma list with suffix" \
    "echo {one,two,three}.txt" \
    "one.txt two.txt three.txt"

run_test "Comma list with prefix and suffix" \
    "echo pre{a,b}post" \
    "preapost prebpost"

# -----------------------------------------------------------------------------
print_section "Numeric Range Expansion"
# -----------------------------------------------------------------------------

run_test "Simple numeric range" \
    "echo {1..5}" \
    "1 2 3 4 5"

run_test "Numeric range from zero" \
    "echo {0..4}" \
    "0 1 2 3 4"

run_test "Larger numeric range" \
    "echo {1..10}" \
    "1 2 3 4 5 6 7 8 9 10"

run_test "Reverse numeric range" \
    "echo {5..1}" \
    "5 4 3 2 1"

run_test "Reverse from 10" \
    "echo {10..1}" \
    "10 9 8 7 6 5 4 3 2 1"

run_test "Negative start" \
    "echo {-3..3}" \
    "-3 -2 -1 0 1 2 3"

run_test "Negative range" \
    "echo {-5..-1}" \
    "-5 -4 -3 -2 -1"

run_test "Negative reverse" \
    "echo {3..-3}" \
    "3 2 1 0 -1 -2 -3"

# -----------------------------------------------------------------------------
print_section "Character Range Expansion"
# -----------------------------------------------------------------------------

run_test "Lowercase a to e" \
    "echo {a..e}" \
    "a b c d e"

run_test "Lowercase full alphabet" \
    "echo {a..z}" \
    "a b c d e f g h i j k l m n o p q r s t u v w x y z"

run_test "Uppercase A to E" \
    "echo {A..E}" \
    "A B C D E"

run_test "Reverse character range" \
    "echo {e..a}" \
    "e d c b a"

run_test "Reverse uppercase" \
    "echo {E..A}" \
    "E D C B A"

run_test "Character range with prefix" \
    "echo item_{a..c}" \
    "item_a item_b item_c"

run_test "Character range with suffix" \
    "echo {x..z}.log" \
    "x.log y.log z.log"

# -----------------------------------------------------------------------------
print_section "Step Values"
# -----------------------------------------------------------------------------

run_test "Step by 2" \
    "echo {1..10..2}" \
    "1 3 5 7 9"

run_test "Step by 3" \
    "echo {1..10..3}" \
    "1 4 7 10"

run_test "Step by 5" \
    "echo {0..20..5}" \
    "0 5 10 15 20"

run_test "Reverse with step" \
    "echo {10..1..2}" \
    "10 8 6 4 2"

run_test "Reverse with step 3" \
    "echo {10..1..3}" \
    "10 7 4 1"

run_test "Character step by 2" \
    "echo {a..z..2}" \
    "a c e g i k m o q s u w y"

run_test "Reverse char step" \
    "echo {z..a..2}" \
    "z x v t r p n l j h f d b"

# -----------------------------------------------------------------------------
print_section "Zero Padding"
# -----------------------------------------------------------------------------

run_test "Zero-padded 01..05" \
    "echo {01..05}" \
    "01 02 03 04 05"

run_test "Zero-padded 001..003" \
    "echo {001..003}" \
    "001 002 003"

run_test "Zero-padded with prefix" \
    "echo file{01..03}.txt" \
    "file01.txt file02.txt file03.txt"

run_test "Zero-padded reverse" \
    "echo {05..01}" \
    "05 04 03 02 01"

run_test "Mixed padding (end has more zeros)" \
    "echo {1..03}" \
    "01 02 03"

run_test "Zero-padded with step" \
    "echo {01..10..2}" \
    "01 03 05 07 09"

# -----------------------------------------------------------------------------
print_section "Prefix and Suffix Combinations"
# -----------------------------------------------------------------------------

run_test "Prefix only" \
    "echo file{1..3}" \
    "file1 file2 file3"

run_test "Suffix only" \
    "echo {1..3}.txt" \
    "1.txt 2.txt 3.txt"

run_test "Both prefix and suffix" \
    "echo file{1..3}.txt" \
    "file1.txt file2.txt file3.txt"

run_test "Long prefix" \
    "echo /path/to/file{1..3}" \
    "/path/to/file1 /path/to/file2 /path/to/file3"

run_test "Complex suffix" \
    "echo img{1..3}.backup.png" \
    "img1.backup.png img2.backup.png img3.backup.png"

run_test "Underscore prefix" \
    "echo test_{a..c}_file" \
    "test_a_file test_b_file test_c_file"

# -----------------------------------------------------------------------------
print_section "For Loop Integration"
# -----------------------------------------------------------------------------

run_test "For loop with numeric range" \
    'for i in {1..3}; do echo "n:$i"; done' \
    "n:1
n:2
n:3"

run_test "For loop with character range" \
    'for c in {a..c}; do echo "c:$c"; done' \
    "c:a
c:b
c:c"

run_test "For loop with comma list" \
    'for x in {foo,bar,baz}; do echo "x:$x"; done' \
    "x:foo
x:bar
x:baz"

run_test "For loop with prefixed range" \
    'for f in file{1..3}.txt; do echo "$f"; done' \
    "file1.txt
file2.txt
file3.txt"

run_test "For loop with step" \
    'for n in {1..5..2}; do echo "$n"; done' \
    "1
3
5"

# -----------------------------------------------------------------------------
print_section "Command Group Distinction"
# -----------------------------------------------------------------------------

run_test "Command group still works" \
    "{ echo hello; }" \
    "hello"

run_test "Command group with multiple commands" \
    "{ echo one; echo two; }" \
    "one
two"

run_test "Command group with variable" \
    "x=test; { echo \$x; }" \
    "test"

# -----------------------------------------------------------------------------
print_section "Edge Cases"
# -----------------------------------------------------------------------------

run_test "Single number (no range)" \
    "echo {5}" \
    "{5}"

run_test "Empty braces" \
    "echo {}" \
    "{}"

# Note: "echo { a,b }" triggers a separate tokenizer issue with command groups
# This is tracked separately from brace expansion and skipped here

run_test "Same start and end" \
    "echo {5..5}" \
    "5"

run_test "Same char start and end" \
    "echo {a..a}" \
    "a"

# Note: Full nested/sequential brace expansion like pre{1..3}mid{a..c}post
# expanding to 9 items is an advanced feature. Currently only the first
# brace pattern expands, leaving subsequent ones literal.

run_test "Multiple braces (first expands)" \
    "echo pre{1..3}mid" \
    "pre1mid pre2mid pre3mid"

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------

echo ""
print_header "TEST SUMMARY"

echo -e "Total tests:  $TOTAL_TESTS"
echo -e "Passed:       ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed:       ${RED}$FAILED_TESTS${NC}"

if [[ $FAILED_TESTS -eq 0 ]]; then
    echo -e "\n${GREEN}All brace expansion tests passed!${NC}\n"
    exit 0
else
    echo -e "\n${RED}Some tests failed.${NC}\n"
    exit 1
fi
