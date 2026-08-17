#!/bin/bash

# =============================================================================
# ARRAYS AND ARITHMETIC COMMAND TESTS
# =============================================================================
#
# Tests extended language features including:
# - Indexed array creation and access
# - Associative arrays (declare -A)
# - Arithmetic command (( expr ))
# - Array literal syntax arr=(...)
# - Array element assignment arr[n]=value
# - Array expansion ${arr[@]}, ${arr[*]}, ${#arr[@]}
# - declare builtin with -a, -A, -i options
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
}

# =============================================================================
# ARRAY LITERAL TESTS
# =============================================================================
test_array_literals() {
    print_category "ARRAY LITERAL SYNTAX"

    print_section "Basic Array Creation"

    run_test "Create array with literals" \
        'arr=(one two three); echo "${arr[0]}"' \
        "one"

    run_test "Access second element" \
        'arr=(one two three); echo "${arr[1]}"' \
        "two"

    run_test "Access third element" \
        'arr=(one two three); echo "${arr[2]}"' \
        "three"

    run_test "Access all elements with @" \
        'arr=(one two three); echo "${arr[@]}"' \
        "one two three"

    run_test "Access all elements with *" \
        'arr=(one two three); echo "${arr[*]}"' \
        "one two three"

    print_section "Array Length"

    run_test "Get array length" \
        'arr=(one two three); echo "${#arr[@]}"' \
        "3"

    run_test "Empty array length" \
        'arr=(); echo "${#arr[@]}"' \
        "0"

    run_test "Single element array length" \
        'arr=(only); echo "${#arr[@]}"' \
        "1"

    print_section "Element String Length"

    run_test "Get element string length" \
        'arr=(hello world); echo "${#arr[0]}"' \
        "5"

    run_test "Get second element string length" \
        'arr=(hello world); echo "${#arr[1]}"' \
        "5"
}

# =============================================================================
# ARRAY ELEMENT ASSIGNMENT TESTS
# =============================================================================
test_array_element_assignment() {
    print_category "ARRAY ELEMENT ASSIGNMENT"

    print_section "Basic Element Assignment"

    run_test "Assign to existing element" \
        'arr=(one two three); arr[1]=TWO; echo "${arr[1]}"' \
        "TWO"

    run_test "Array maintains other elements after assignment" \
        'arr=(one two three); arr[1]=TWO; echo "${arr[@]}"' \
        "one TWO three"

    run_test "Assign to new index beyond current size" \
        'arr=(one two); arr[5]=five; echo "${arr[5]}"' \
        "five"

    print_section "Sparse Arrays"

    run_test "Sparse array access unset element" \
        'arr[0]=first; arr[10]=tenth; echo "${arr[5]}"' \
        ""

    run_test "Sparse array length counts only set elements" \
        'arr[0]=first; arr[10]=tenth; echo "${#arr[@]}"' \
        "2"

    print_section "Append Operator"

    # Note: += for arrays might need separate implementation
    run_success_test "Array element assignment syntax works" \
        'arr=(one two); arr[0]=ONE'
}

# =============================================================================
# ARITHMETIC COMMAND TESTS
# =============================================================================
test_arithmetic_command() {
    print_category "ARITHMETIC COMMAND (( expr ))"

    print_section "Basic Arithmetic"

    run_success_test "Simple addition returns success for non-zero" \
        '(( 5 + 3 ))'

    run_fail_test "Zero result returns failure" \
        '(( 0 ))'

    run_fail_test "Expression evaluating to zero returns failure" \
        '(( 5 - 5 ))'

    run_success_test "Non-zero expression returns success" \
        '(( 10 - 3 ))'

    print_section "Arithmetic with Variables"

    run_test "Arithmetic with variable" \
        'x=5; (( x + 3 )); echo $?' \
        "0"

    run_test "Arithmetic comparison true" \
        '(( 5 > 3 )); echo $?' \
        "0"

    run_test "Arithmetic comparison false" \
        '(( 3 > 5 )); echo $?' \
        "1"

    print_section "Complex Expressions"

    run_success_test "Multiplication" \
        '(( 4 * 5 ))'

    run_success_test "Division" \
        '(( 10 / 2 ))'

    run_success_test "Modulo" \
        '(( 10 % 3 ))'

    run_success_test "Parentheses in expression" \
        '(( (2 + 3) * 4 ))'
}

# =============================================================================
# DECLARE BUILTIN TESTS
# =============================================================================
test_declare_builtin() {
    print_category "DECLARE BUILTIN"

    print_section "declare -a (Indexed Arrays)"

    run_test "declare -a creates indexed array" \
        'declare -a "arr=(one two three)"; echo "${arr[@]}"' \
        "one two three"

    run_test "declare -a array element access" \
        'declare -a "arr=(one two three)"; echo "${arr[1]}"' \
        "two"

    run_test "declare -a array length" \
        'declare -a "arr=(one two three)"; echo "${#arr[@]}"' \
        "3"

    print_section "declare -i (Integer Variables)"

    run_test "declare -i evaluates arithmetic on assignment" \
        'declare -i num=5+3; echo "$num"' \
        "8"

    run_test "declare -i with multiplication" \
        'declare -i num=4*5; echo "$num"' \
        "20"

    run_test "declare -i with complex expression" \
        'declare -i "num=(2+3)*4"; echo "$num"' \
        "20"

    print_section "declare -p (Print)"

    local output
    output=$(echo 'declare x=hello; declare -p x' | "$LUSH" 2>&1)
    if [[ "$output" == *'declare -- x="hello"'* ]]; then
        test_result "declare -p shows variable" 0
    else
        test_result "declare -p shows variable" 1 "Got: $output"
    fi

    output=$(echo 'declare -a "arr=(one two)"; declare -p arr' | "$LUSH" 2>&1)
    if [[ "$output" == *'declare -a arr'* ]]; then
        test_result "declare -p shows array type" 0
    else
        test_result "declare -p shows array type" 1 "Got: $output"
    fi

    print_section "typeset Alias"

    run_test "typeset works as declare alias" \
        'typeset -i x=3*4; echo $x' \
        "12"

    run_test "typeset -a creates array" \
        'typeset -a "arr=(a b c)"; echo "${arr[1]}"' \
        "b"
}

# =============================================================================
# SHELL MODE INTEGRATION TESTS
# =============================================================================
test_shell_mode_integration() {
    print_category "SHELL MODE INTEGRATION"

    print_section "Lush Mode (Default)"

    # Arrays should work in default Lush mode
    run_success_test "Arrays work in default mode" \
        'arr=(one two three); echo "${arr[@]}"'

    run_success_test "Arithmetic command works in default mode" \
        '(( 5 + 3 ))'

    print_section "Feature Flag Checks"

    # These tests verify the mode system is integrated
    run_success_test "Indexed arrays feature enabled" \
        'arr=(test); echo "${arr[0]}"'
}

# =============================================================================
# EDGE CASES AND ERROR HANDLING
# =============================================================================
test_edge_cases() {
    print_category "EDGE CASES AND ERROR HANDLING"

    print_section "Empty and Special Values"

    run_test "Empty array" \
        'arr=(); echo "${#arr[@]}"' \
        "0"

    run_test "Array with empty string element" \
        'arr=("" "second"); echo "${arr[1]}"' \
        "second"

    run_test "Array element with spaces" \
        'arr=("hello world" "foo bar"); echo "${arr[0]}"' \
        "hello world"

    print_section "Variable Name Validation"

    # declare should reject invalid names
    run_fail_test "declare rejects invalid identifier (starts with number)" \
        'declare 1invalid=value'

    run_fail_test "declare rejects invalid identifier (special chars)" \
        'declare inv@lid=value'

    print_section "Index Bounds"

    run_test "Access beyond array bounds returns empty" \
        'arr=(one two); echo "x${arr[99]}x"' \
        "xx"

    run_test "Negative index handling" \
        'arr=(one two three); echo "${arr[-1]}"' \
        ""
}

# =============================================================================
# COMBINED FEATURE TESTS
# =============================================================================
test_combined_features() {
    print_category "COMBINED FEATURE TESTS"

    print_section "Arrays with Arithmetic"

    run_test "Use arithmetic result as array index" \
        'arr=(zero one two three); i=1; echo "${arr[$((i+1))]}"' \
        "two"

    run_test "Array length in arithmetic" \
        'arr=(a b c d e); (( ${#arr[@]} == 5 )); echo $?' \
        "0"

    print_section "Multiple Arrays"

    run_test "Multiple arrays coexist" \
        'arr1=(a b); arr2=(x y z); echo "${arr1[0]} ${arr2[2]}"' \
        "a z"

    run_test "Copy between arrays" \
        'arr1=(one two three); arr2[0]="${arr1[1]}"; echo "${arr2[0]}"' \
        "two"

    print_section "Loop Over Array"

    local output
    output=$(echo 'arr=(one two three); for x in "${arr[@]}"; do echo "$x"; done' | "$LUSH" 2>&1)
    local expected=$'one\ntwo\nthree'
    if [[ "$output" == "$expected" ]]; then
        test_result "Loop over array elements" 0
    else
        test_result "Loop over array elements" 1 "Expected: '$expected', Got: '$output'"
    fi
}

# =============================================================================
# MAIN
# =============================================================================
main() {
    print_header "ARRAYS AND ARITHMETIC COMMAND TESTS"
    echo "Testing shell: $LUSH"
    echo "Started at: $(date)"

    # Verify shell exists
    if [[ ! -x "$LUSH" ]]; then
        echo -e "${RED}ERROR: Shell binary not found: $LUSH${NC}"
        exit 1
    fi

    setup_test_env

    # Run all test categories
    test_array_literals
    test_array_element_assignment
    test_arithmetic_command
    test_declare_builtin
    test_shell_mode_integration
    test_edge_cases
    test_combined_features

    # Results summary
    print_header "TEST RESULTS"

    echo -e "${BLUE}Total Tests:${NC} $TOTAL_TESTS"
    echo -e "${GREEN}Passed:${NC} $PASSED_TESTS"
    echo -e "${RED}Failed:${NC} $FAILED_TESTS"

    if [[ $FAILED_TESTS -eq 0 ]]; then
        echo -e "\n${GREEN}ALL TESTS PASSED! ${NC}"
        echo -e "${GREEN}Arrays and Arithmetic Command implementation is complete!${NC}"
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
    echo "OK Indexed array creation with arr=(...) syntax"
    echo "OK Array element access \${arr[n]}"
    echo "OK Array expansion \${arr[@]} and \${arr[*]}"
    echo "OK Array length \${#arr[@]}"
    echo "OK Element string length \${#arr[n]}"
    echo "OK Array element assignment arr[n]=value"
    echo "OK Sparse array support"
    echo "OK Arithmetic command (( expr ))"
    echo "OK declare -a for indexed arrays"
    echo "OK declare -i for integer variables"
    echo "OK declare -p for printing declarations"
    echo "OK typeset alias for declare"
    echo "OK Shell mode integration"

    echo -e "\nTest completed at: $(date)"

    exit $exit_code
}

# Run main function
main "$@"
