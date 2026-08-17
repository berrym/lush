#!/bin/bash

# =============================================================================
# EXTENDED TEST [[ ]] TESTS
# =============================================================================
#
# Tests extended language features including:
# - Extended test [[ ]] syntax
# - String comparisons (==, !=, <, >)
# - Pattern matching with glob patterns
# - Regex matching with =~ operator
# - BASH_REMATCH array for capture groups
# - File tests (-f, -d, -e, -r, -w, -x, etc.)
# - String tests (-z, -n)
# - Numeric comparisons (-eq, -ne, -lt, -le, -gt, -ge)
# - Logical operators (&&, ||, !)
#
# Author: AI Assistant for Lush Extended Language Implementation
# =============================================================================

set -euo pipefail

# Get absolute path to lush binary
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
LUSH="${1:-$PROJECT_DIR/build/lush}"
TEST_DIR="/tmp/lush__test_$$"
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

# Cleanup on exit
cleanup() {
    cd /
    rm -rf "$TEST_DIR" 2>/dev/null || true
}
trap cleanup EXIT

print_header() {
    echo -e "\n${BLUE}===============================================================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}===============================================================================${NC}\n"
}

print_section() {
    echo -e "\n${CYAN}=== $1 ===${NC}"
}

print_category() {
    echo -e "\n${PURPLE}### $1 ###${NC}"
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

    if [[ "$output" == "$expected" ]]; then
        test_result "$test_name" 0
    else
        test_result "$test_name" 1 "Expected: '$expected', Got: '$output'"
    fi
}

# Helper to check if command succeeds (exit code 0)
run_success_test() {
    local test_name="$1"
    local command="$2"

    if echo "$command" | "$LUSH" >/dev/null 2>&1; then
        test_result "$test_name" 0
    else
        test_result "$test_name" 1
    fi
}

# Helper to check if command fails (exit code != 0)
run_fail_test() {
    local test_name="$1"
    local command="$2"

    if echo "$command" | "$LUSH" >/dev/null 2>&1; then
        test_result "$test_name" 1 "Expected failure but succeeded"
    else
        test_result "$test_name" 0
    fi
}

# Setup test environment
setup_test_env() {
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"

    # Create test files for file tests
    echo "test content" > "$TEST_DIR/testfile.txt"
    mkdir -p "$TEST_DIR/testdir"
    touch "$TEST_DIR/readable.txt"
    chmod 644 "$TEST_DIR/readable.txt"
    touch "$TEST_DIR/writable.txt"
    chmod 666 "$TEST_DIR/writable.txt"
    touch "$TEST_DIR/executable.sh"
    chmod 755 "$TEST_DIR/executable.sh"
    touch "$TEST_DIR/empty.txt"
    ln -s "$TEST_DIR/testfile.txt" "$TEST_DIR/symlink.txt"
}

# =============================================================================
# STRING EQUALITY TESTS
# =============================================================================
test_string_equality() {
    print_category "STRING EQUALITY"

    print_section "Basic String Comparisons"

    run_test "String equality true" \
        '[[ "hello" == "hello" ]]; echo $?' \
        "0"

    run_test "String equality false" \
        '[[ "hello" == "world" ]]; echo $?' \
        "1"

    run_test "String inequality true" \
        '[[ "hello" != "world" ]]; echo $?' \
        "0"

    run_test "String inequality false" \
        '[[ "hello" != "hello" ]]; echo $?' \
        "1"

    print_section "String Comparisons with Variables"

    run_test "Variable string equality" \
        'x="hello"; [[ "$x" == "hello" ]]; echo $?' \
        "0"

    run_test "Two variables equality" \
        'x="test"; y="test"; [[ "$x" == "$y" ]]; echo $?' \
        "0"

    run_test "Two variables inequality" \
        'x="foo"; y="bar"; [[ "$x" != "$y" ]]; echo $?' \
        "0"

    print_section "Lexicographic Comparisons"

    run_test "String less than true" \
        '[[ "apple" < "banana" ]]; echo $?' \
        "0"

    run_test "String less than false" \
        '[[ "cherry" < "banana" ]]; echo $?' \
        "1"

    run_test "String greater than true" \
        '[[ "zebra" > "apple" ]]; echo $?' \
        "0"

    run_test "String greater than false" \
        '[[ "apple" > "zebra" ]]; echo $?' \
        "1"
}

# =============================================================================
# PATTERN MATCHING TESTS
# =============================================================================
test_pattern_matching() {
    print_category "PATTERN MATCHING"

    print_section "Basic Glob Patterns"

    run_test "Pattern match with * (match)" \
        '[[ "hello.txt" == *.txt ]]; echo $?' \
        "0"

    run_test "Pattern match with * (no match)" \
        '[[ "hello.txt" == *.md ]]; echo $?' \
        "1"

    run_test "Pattern match prefix" \
        '[[ "test_file.sh" == test* ]]; echo $?' \
        "0"

    run_test "Pattern match prefix (no match)" \
        '[[ "prod_file.sh" == test* ]]; echo $?' \
        "1"

    print_section "Single Character Match"

    run_test "Pattern match with ? (match)" \
        '[[ "cat" == c?t ]]; echo $?' \
        "0"

    run_test "Pattern match with ? (no match)" \
        '[[ "cart" == c?t ]]; echo $?' \
        "1"

    run_test "Multiple ? wildcards" \
        '[[ "hello" == h?ll? ]]; echo $?' \
        "0"

    print_section "Character Classes"

    run_test "Character class match" \
        '[[ "cat" == [abc]at ]]; echo $?' \
        "0"

    run_test "Character class no match" \
        '[[ "rat" == [abc]at ]]; echo $?' \
        "1"

    run_test "Character range match" \
        '[[ "file5" == file[0-9] ]]; echo $?' \
        "0"

    print_section "Combined Patterns"

    run_test "Complex pattern match" \
        '[[ "test_file_01.txt" == test_*_[0-9][0-9].txt ]]; echo $?' \
        "0"

    run_test "Pattern with multiple *" \
        '[[ "a/b/c/d.txt" == */*/*.txt ]]; echo $?' \
        "0"
}

# =============================================================================
# REGEX MATCHING TESTS
# =============================================================================
test_regex_matching() {
    print_category "REGEX MATCHING"

    print_section "Basic Regex"

    run_test "Simple regex match" \
        '[[ "hello123" =~ [0-9]+ ]]; echo $?' \
        "0"

    run_test "Simple regex no match" \
        '[[ "hello" =~ [0-9]+ ]]; echo $?' \
        "1"

    run_test "Word boundary regex" \
        '[[ "hello world" =~ world ]]; echo $?' \
        "0"

    run_test "Start anchor" \
        '[[ "hello" =~ ^hello ]]; echo $?' \
        "0"

    run_test "End anchor" \
        '[[ "hello" =~ ello$ ]]; echo $?' \
        "0"

    run_test "Full line anchor" \
        '[[ "hello" =~ ^hello$ ]]; echo $?' \
        "0"

    print_section "Regex with Capture Groups"

    run_test "Single capture group" \
        '[[ "hello123" =~ ([0-9]+) ]]; echo "${BASH_REMATCH[1]}"' \
        "123"

    run_test "BASH_REMATCH[0] contains full match" \
        '[[ "hello123" =~ ([0-9]+) ]]; echo "${BASH_REMATCH[0]}"' \
        "123"

    run_test "Two capture groups" \
        '[[ "hello123world" =~ ([a-z]+)([0-9]+) ]]; echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"' \
        "hello 123"

    run_test "Three capture groups" \
        '[[ "abc123xyz" =~ ([a-z]+)([0-9]+)([a-z]+) ]]; echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]} ${BASH_REMATCH[3]}"' \
        "abc 123 xyz"

    print_section "Complex Regex Patterns"

    run_test "Email-like pattern" \
        '[[ "user@example.com" =~ ([a-z]+)@([a-z]+)\.com ]]; echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"' \
        "user example"

    run_test "Date-like pattern" \
        '[[ "2024-01-15" =~ ([0-9]+)-([0-9]+)-([0-9]+) ]]; echo "${BASH_REMATCH[1]}-${BASH_REMATCH[2]}-${BASH_REMATCH[3]}"' \
        "2024-01-15"

    run_test "Optional group" \
        '[[ "color" =~ colou?r ]]; echo $?' \
        "0"

    run_test "Alternation" \
        '[[ "cat" =~ cat|dog ]]; echo $?' \
        "0"

    run_test "Alternation second option" \
        '[[ "dog" =~ cat|dog ]]; echo $?' \
        "0"
}

# =============================================================================
# STRING TESTS
# =============================================================================
test_string_tests() {
    print_category "STRING TESTS"

    print_section "Empty String Tests"

    run_test "-z empty string true" \
        '[[ -z "" ]]; echo $?' \
        "0"

    run_test "-z non-empty string false" \
        '[[ -z "hello" ]]; echo $?' \
        "1"

    run_test "-n non-empty string true" \
        '[[ -n "hello" ]]; echo $?' \
        "0"

    run_test "-n empty string false" \
        '[[ -n "" ]]; echo $?' \
        "1"

    print_section "String Tests with Variables"

    run_test "-z empty variable" \
        'x=""; [[ -z "$x" ]]; echo $?' \
        "0"

    run_test "-z unset variable" \
        'unset x 2>/dev/null; [[ -z "$x" ]]; echo $?' \
        "0"

    run_test "-n set variable" \
        'x="value"; [[ -n "$x" ]]; echo $?' \
        "0"
}

# =============================================================================
# FILE TESTS
# =============================================================================
test_file_tests() {
    print_category "FILE TESTS"

    print_section "File Existence Tests"

    run_test "-e existing file" \
        "[[ -e '$TEST_DIR/testfile.txt' ]]; echo \$?" \
        "0"

    run_test "-e non-existing file" \
        "[[ -e '$TEST_DIR/nonexistent.txt' ]]; echo \$?" \
        "1"

    run_test "-f regular file" \
        "[[ -f '$TEST_DIR/testfile.txt' ]]; echo \$?" \
        "0"

    run_test "-f directory (should fail)" \
        "[[ -f '$TEST_DIR/testdir' ]]; echo \$?" \
        "1"

    run_test "-d directory" \
        "[[ -d '$TEST_DIR/testdir' ]]; echo \$?" \
        "0"

    run_test "-d regular file (should fail)" \
        "[[ -d '$TEST_DIR/testfile.txt' ]]; echo \$?" \
        "1"

    print_section "File Permission Tests"

    run_test "-r readable file" \
        "[[ -r '$TEST_DIR/readable.txt' ]]; echo \$?" \
        "0"

    run_test "-w writable file" \
        "[[ -w '$TEST_DIR/writable.txt' ]]; echo \$?" \
        "0"

    run_test "-x executable file" \
        "[[ -x '$TEST_DIR/executable.sh' ]]; echo \$?" \
        "0"

    print_section "File Size Tests"

    run_test "-s non-empty file" \
        "[[ -s '$TEST_DIR/testfile.txt' ]]; echo \$?" \
        "0"

    run_test "-s empty file" \
        "[[ -s '$TEST_DIR/empty.txt' ]]; echo \$?" \
        "1"

    print_section "File Type Tests"

    run_test "-L symlink" \
        "[[ -L '$TEST_DIR/symlink.txt' ]]; echo \$?" \
        "0"

    run_test "-L regular file (should fail)" \
        "[[ -L '$TEST_DIR/testfile.txt' ]]; echo \$?" \
        "1"

    run_test "-h symlink (alias for -L)" \
        "[[ -h '$TEST_DIR/symlink.txt' ]]; echo \$?" \
        "0"
}

# =============================================================================
# NUMERIC COMPARISON TESTS
# =============================================================================
test_numeric_comparisons() {
    print_category "NUMERIC COMPARISONS"

    print_section "Equality and Inequality"

    run_test "-eq equal numbers" \
        '[[ 5 -eq 5 ]]; echo $?' \
        "0"

    run_test "-eq unequal numbers" \
        '[[ 5 -eq 3 ]]; echo $?' \
        "1"

    run_test "-ne unequal numbers" \
        '[[ 5 -ne 3 ]]; echo $?' \
        "0"

    run_test "-ne equal numbers" \
        '[[ 5 -ne 5 ]]; echo $?' \
        "1"

    print_section "Less Than / Greater Than"

    run_test "-lt less than true" \
        '[[ 3 -lt 5 ]]; echo $?' \
        "0"

    run_test "-lt less than false" \
        '[[ 5 -lt 3 ]]; echo $?' \
        "1"

    run_test "-gt greater than true" \
        '[[ 5 -gt 3 ]]; echo $?' \
        "0"

    run_test "-gt greater than false" \
        '[[ 3 -gt 5 ]]; echo $?' \
        "1"

    print_section "Less/Greater Than or Equal"

    run_test "-le less than" \
        '[[ 3 -le 5 ]]; echo $?' \
        "0"

    run_test "-le equal" \
        '[[ 5 -le 5 ]]; echo $?' \
        "0"

    run_test "-le greater than" \
        '[[ 5 -le 3 ]]; echo $?' \
        "1"

    run_test "-ge greater than" \
        '[[ 5 -ge 3 ]]; echo $?' \
        "0"

    run_test "-ge equal" \
        '[[ 5 -ge 5 ]]; echo $?' \
        "0"

    run_test "-ge less than" \
        '[[ 3 -ge 5 ]]; echo $?' \
        "1"

    print_section "Numeric Comparisons with Variables"

    run_test "Variable comparison -lt" \
        'x=10; y=20; [[ $x -lt $y ]]; echo $?' \
        "0"

    run_test "Variable comparison -eq" \
        'x=15; y=15; [[ $x -eq $y ]]; echo $?' \
        "0"
}

# =============================================================================
# LOGICAL OPERATOR TESTS
# =============================================================================
test_logical_operators() {
    print_category "LOGICAL OPERATORS"

    print_section "Negation"

    run_test "! negates true to false" \
        '[[ ! "hello" == "hello" ]]; echo $?' \
        "1"

    run_test "! negates false to true" \
        '[[ ! "hello" == "world" ]]; echo $?' \
        "0"

    print_section "AND Operator"

    run_test "&& both true" \
        '[[ "a" == "a" && "b" == "b" ]]; echo $?' \
        "0"

    run_test "&& first false" \
        '[[ "a" == "x" && "b" == "b" ]]; echo $?' \
        "1"

    run_test "&& second false" \
        '[[ "a" == "a" && "b" == "x" ]]; echo $?' \
        "1"

    run_test "&& both false" \
        '[[ "a" == "x" && "b" == "y" ]]; echo $?' \
        "1"

    print_section "OR Operator"

    run_test "|| both true" \
        '[[ "a" == "a" || "b" == "b" ]]; echo $?' \
        "0"

    run_test "|| first true" \
        '[[ "a" == "a" || "b" == "x" ]]; echo $?' \
        "0"

    run_test "|| second true" \
        '[[ "a" == "x" || "b" == "b" ]]; echo $?' \
        "0"

    run_test "|| both false" \
        '[[ "a" == "x" || "b" == "y" ]]; echo $?' \
        "1"

    print_section "Combined Operators"

    run_test "Complex: && with ||" \
        '[[ "a" == "a" && "b" == "x" || "c" == "c" ]]; echo $?' \
        "0"

    run_test "Complex: ! with &&" \
        '[[ ! "a" == "x" && "b" == "b" ]]; echo $?' \
        "0"

    run_test "Parentheses grouping" \
        '[[ ( "a" == "a" || "b" == "x" ) && "c" == "c" ]]; echo $?' \
        "0"
}

# =============================================================================
# COMBINED FEATURE TESTS
# =============================================================================
test_combined_features() {
    print_category "COMBINED FEATURE TESTS"

    print_section "If Statements with [[ ]]"

    run_test "If with [[ ]] true branch" \
        'if [[ "hello" == "hello" ]]; then echo "yes"; else echo "no"; fi' \
        "yes"

    run_test "If with [[ ]] false branch" \
        'if [[ "hello" == "world" ]]; then echo "yes"; else echo "no"; fi' \
        "no"

    print_section "While Loops with [[ ]]"

    run_test "While loop with [[ ]]" \
        'i=0; while [[ $i -lt 3 ]]; do echo $i; i=$((i+1)); done' \
        $'0\n1\n2'

    print_section "Regex in Conditionals"

    run_test "If with regex match" \
        'x="hello123"; if [[ "$x" =~ ([a-z]+)([0-9]+) ]]; then echo "${BASH_REMATCH[1]}"; fi' \
        "hello"

    print_section "Mixed Tests"

    run_test "File test with string comparison" \
        "if [[ -f '$TEST_DIR/testfile.txt' && 'test' == 'test' ]]; then echo 'yes'; fi" \
        "yes"

    run_test "Regex with logical operators" \
        '[[ "abc123" =~ [0-9]+ && "xyz" == "xyz" ]]; echo $?' \
        "0"
}

# =============================================================================
# EDGE CASES
# =============================================================================
test_edge_cases() {
    print_category "EDGE CASES"

    print_section "Empty Strings in Comparisons"

    run_test "Empty string equals empty" \
        '[[ "" == "" ]]; echo $?' \
        "0"

    run_test "Empty string not equals non-empty" \
        '[[ "" == "x" ]]; echo $?' \
        "1"

    print_section "Special Characters"

    run_test "String with spaces" \
        '[[ "hello world" == "hello world" ]]; echo $?' \
        "0"

    run_test "String with special chars in quotes" \
        '[[ "a*b" == "a*b" ]]; echo $?' \
        "0"

    print_section "Unquoted Variables"

    run_test "Unquoted empty variable in [[ ]]" \
        'x=""; [[ $x == "" ]]; echo $?' \
        "0"

    run_test "Unquoted variable with spaces" \
        'x="hello world"; [[ $x == "hello world" ]]; echo $?' \
        "0"
}

# =============================================================================
# MAIN
# =============================================================================
main() {
    print_header "EXTENDED TEST [[ ]] TESTS"
    echo "Testing shell: $LUSH"
    echo "Started at: $(date)"

    # Verify shell exists
    if [[ ! -x "$LUSH" ]]; then
        echo -e "${RED}ERROR: Shell binary not found: $LUSH${NC}"
        exit 1
    fi

    setup_test_env

    # Run all test categories
    test_string_equality
    test_pattern_matching
    test_regex_matching
    test_string_tests
    test_file_tests
    test_numeric_comparisons
    test_logical_operators
    test_combined_features
    test_edge_cases

    # Results summary
    print_header "TEST RESULTS"

    echo -e "${BLUE}Total Tests:${NC} $TOTAL_TESTS"
    echo -e "${GREEN}Passed:${NC} $PASSED_TESTS"
    echo -e "${RED}Failed:${NC} $FAILED_TESTS"

    if [[ $FAILED_TESTS -eq 0 ]]; then
        echo -e "\n${GREEN}ALL TESTS PASSED!${NC}"
        echo -e "${GREEN}Extended Test [[ ]] implementation is complete!${NC}"
        exit_code=0
    else
        local pass_rate=$((PASSED_TESTS * 100 / TOTAL_TESTS))
        echo -e "\n${YELLOW}Pass Rate: ${pass_rate}%${NC}"

        if [[ $pass_rate -ge 90 ]]; then
            echo -e "${GREEN}EXCELLENT${NC} - nearly complete"
            exit_code=0
        elif [[ $pass_rate -ge 80 ]]; then
            echo -e "${YELLOW}GOOD${NC} - Most features working"
            exit_code=0
        elif [[ $pass_rate -ge 70 ]]; then
            echo -e "${YELLOW}FAIR${NC} - Some features need work"
            exit_code=1
        else
            echo -e "${RED}NEEDS WORK${NC} - implementation incomplete"
            exit_code=2
        fi
    fi

    echo -e "\n${CYAN}Features Tested:${NC}"
    echo "- String equality (==, !=)"
    echo "- Lexicographic comparisons (<, >)"
    echo "- Pattern matching with glob wildcards (*, ?, [...])"
    echo "- Regex matching with =~ operator"
    echo "- BASH_REMATCH array for capture groups"
    echo "- String tests (-z, -n)"
    echo "- File tests (-e, -f, -d, -r, -w, -x, -s, -L, -h)"
    echo "- Numeric comparisons (-eq, -ne, -lt, -le, -gt, -ge)"
    echo "- Logical operators (!, &&, ||)"
    echo "- Parentheses grouping"
    echo "- Integration with if/while statements"

    echo -e "\nTest completed at: $(date)"

    exit $exit_code
}

# Run main function
main "$@"
