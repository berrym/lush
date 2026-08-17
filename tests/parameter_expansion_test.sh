#!/bin/bash

# =============================================================================
# EXTENDED PARAMETER EXPANSION TESTS
# =============================================================================
#
# Tests extended language features including:
# - Case modification: ${var^^}, ${var,,}, ${var^}, ${var,}
# - Substring extraction: ${var:offset:length}
# - Pattern substitution: ${var/pattern/replacement}, ${var//pattern/replacement}
# - Indirect expansion: ${!name}, ${!prefix*}
# - Transformations: ${var@Q}, ${var@E}, ${var@A}, ${var@U}, ${var@L}
#
# Author: AI Assistant for Lush Extended Language Implementation
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

print_category() {
    echo -e "\n${PURPLE}--- $1 ---${NC}"
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

# Helper to check if output contains expected string
run_contains_test() {
    local test_name="$1"
    local command="$2"
    local expected="$3"

    local output
    output=$(echo "$command" | "$LUSH" 2>&1) || true

    if [[ "$output" == *"$expected"* ]]; then
        test_result "$test_name" 0
    else
        test_result "$test_name" 1 "Expected to contain: '$expected', Got: '$output'"
    fi
}

# =============================================================================
# CASE MODIFICATION TESTS
# =============================================================================

test_case_modification() {
    print_section "Case Modification"

    print_category "Uppercase All \${var^^}"

    run_test '${var^^} basic' \
        'x=hello; echo "${x^^}"' \
        'HELLO'

    run_test '${var^^} mixed case' \
        'x=HeLLo; echo "${x^^}"' \
        'HELLO'

    run_test '${var^^} with numbers' \
        'x=abc123def; echo "${x^^}"' \
        'ABC123DEF'

    print_category "Lowercase All \${var,,}"

    run_test '${var,,} basic' \
        'x=HELLO; echo "${x,,}"' \
        'hello'

    run_test '${var,,} mixed case' \
        'x=HeLLo; echo "${x,,}"' \
        'hello'

    print_category "Uppercase First \${var^}"

    run_test '${var^} basic' \
        'x=hello; echo "${x^}"' \
        'Hello'

    run_test '${var^} already upper' \
        'x=HELLO; echo "${x^}"' \
        'HELLO'

    print_category "Lowercase First \${var,}"

    run_test '${var,} basic' \
        'x=HELLO; echo "${x,}"' \
        'hELLO'

    run_test '${var,} already lower' \
        'x=hello; echo "${x,}"' \
        'hello'
}

# =============================================================================
# SUBSTRING EXTRACTION TESTS
# =============================================================================

test_substring() {
    print_section "Substring Extraction"

    print_category "Basic \${var:offset:length}"

    run_test '${var:0:5} first 5 chars' \
        'x=hello_world; echo "${x:0:5}"' \
        'hello'

    run_test '${var:6:5} middle 5 chars' \
        'x=hello_world; echo "${x:6:5}"' \
        'world'

    run_test '${var:6} from offset to end' \
        'x=hello_world; echo "${x:6}"' \
        'world'

    print_category "Negative Offsets"

    run_test '${var: -5} last 5 chars' \
        'x=hello_world; echo "${x: -5}"' \
        'world'

    print_category "Edge Cases"

    run_test '${var:0:0} zero length' \
        'x=hello; echo "${x:0:0}"' \
        ''

    run_test '${var:100} beyond length' \
        'x=hello; echo "${x:100}"' \
        ''
}

# =============================================================================
# PATTERN SUBSTITUTION TESTS
# =============================================================================

test_pattern_substitution() {
    print_section "Pattern Substitution"

    print_category "First Match \${var/pattern/replacement}"

    run_test '${var/l/L} replace first l' \
        'x=hello; echo "${x/l/L}"' \
        'heLlo'

    run_test '${var/o/0} replace first o' \
        'x=hello; echo "${x/o/0}"' \
        'hell0'

    run_test '${var/x/X} no match' \
        'x=hello; echo "${x/x/X}"' \
        'hello'

    print_category "All Matches \${var//pattern/replacement}"

    run_test '${var//l/L} replace all l' \
        'x=hello; echo "${x//l/L}"' \
        'heLLo'

    run_test '${var//o/0} replace all o' \
        'x=foo; echo "${x//o/0}"' \
        'f00'

    print_category "Delete Pattern (empty replacement)"

    run_test '${var/l/} delete first l' \
        'x=hello; echo "${x/l/}"' \
        'helo'

    run_test '${var//l/} delete all l' \
        'x=hello; echo "${x//l/}"' \
        'heo'

    print_category "Pattern with Wildcards"

    run_test '${var/h*/X} glob pattern' \
        'x=hello; echo "${x/h*/X}"' \
        'X'

    run_test '${var/l?/X} single char wildcard' \
        'x=hello; echo "${x/l?/X}"' \
        'heXo'
}

# =============================================================================
# INDIRECT EXPANSION TESTS
# =============================================================================

test_indirect_expansion() {
    print_section "Indirect Expansion"

    print_category "Simple Indirect \${!name}"

    run_test '${!name} basic indirect' \
        'x=hello; y=x; echo "${!y}"' \
        'hello'

    run_test '${!name} chain indirect' \
        'a=value; b=a; echo "${!b}"' \
        'value'

    run_test '${!name} unset variable' \
        'y=nonexistent; echo "${!y}"' \
        ''

    print_category "Array Keys \${!arr[@]}"

    run_test '${!arr[@]} indexed array keys' \
        'arr=(a b c); echo "${!arr[@]}"' \
        '0 1 2'
}

# =============================================================================
# TRANSFORMATION TESTS
# =============================================================================

test_transformations() {
    print_section "Transformations"

    print_category "Quote \${var@Q}"

    run_test '${var@Q} simple string' \
        "x=hello; echo \"\${x@Q}\"" \
        "'hello'"

    run_contains_test '${var@Q} with spaces' \
        'x="hello world"; echo "${x@Q}"' \
        'hello world'

    print_category "Escape \${var@E}"

    run_contains_test '${var@E} newline' \
        'x="hello\\nworld"; echo "${x@E}"' \
        $'hello\nworld'

    run_contains_test '${var@E} tab' \
        'x="hello\\tworld"; echo "${x@E}"' \
        $'hello\tworld'

    print_category "Assignment \${var@A}"

    run_test '${var@A} assignment form' \
        "x=hello; echo \"\${x@A}\"" \
        "x='hello'"

    print_category "Case Transformations via @"

    run_test '${var@U} uppercase via @' \
        'x=hello; echo "${x@U}"' \
        'HELLO'

    run_test '${var@L} lowercase via @' \
        'x=HELLO; echo "${x@L}"' \
        'hello'

    run_test '${var@u} capitalize via @' \
        'x=hello; echo "${x@u}"' \
        'Hello'
}

# =============================================================================
# COMBINED TESTS
# =============================================================================

test_combined() {
    print_section "Combined Expansions"

    print_category "Multiple Operations"

    run_test 'Substitute then uppercase' \
        'x=hello; y="${x/l/L}"; echo "${y^^}"' \
        'HELLO'

    run_test 'Substring then case' \
        'x=hello_WORLD; echo "${x:6:5}"; echo "${x:0:5}"' \
        $'WORLD\nhello'
}

# =============================================================================
# EXISTING FEATURES REGRESSION
# =============================================================================

test_existing_features() {
    print_section "Existing Features Regression"

    print_category "Default Values"

    run_test '${var:-default} unset' \
        'echo "${unset_var:-default}"' \
        'default'

    run_test '${var:-default} set' \
        'x=value; echo "${x:-default}"' \
        'value'

    print_category "Pattern Removal"

    run_test '${var#pattern} prefix' \
        'x=hello.txt; echo "${x#*.}"' \
        'txt'

    run_test '${var%pattern} suffix' \
        'x=hello.txt; echo "${x%.*}"' \
        'hello'

    print_category "String Length"

    run_test '${#var} length' \
        'x=hello; echo "${#x}"' \
        '5'
}

# =============================================================================
# MAIN TEST RUNNER
# =============================================================================

main() {
    print_header "EXTENDED PARAMETER EXPANSION TESTS"

    echo -e "${CYAN}Lush binary: $LUSH${NC}"

    # Check if lush exists
    if [[ ! -x "$LUSH" ]]; then
        echo -e "${RED}Error: Lush binary not found or not executable: $LUSH${NC}"
        exit 1
    fi

    # Run all test categories
    test_case_modification
    test_substring
    test_pattern_substitution
    test_indirect_expansion
    test_transformations
    test_combined
    test_existing_features

    # Summary
    print_header "TEST SUMMARY"

    echo -e "Total tests:  ${BLUE}$TOTAL_TESTS${NC}"
    echo -e "Passed:       ${GREEN}$PASSED_TESTS${NC}"
    echo -e "Failed:       ${RED}$FAILED_TESTS${NC}"

    if [[ $FAILED_TESTS -eq 0 ]]; then
        echo -e "\n${GREEN}All tests passed!${NC}"
        exit 0
    else
        echo -e "\n${RED}Some tests failed.${NC}"
        exit 1
    fi
}

main "$@"
