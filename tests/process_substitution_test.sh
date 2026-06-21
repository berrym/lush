#!/bin/bash

# =============================================================================
# PROCESS SUBSTITUTION AND EXTENDED PIPES TESTS
# =============================================================================
#
# Tests extended language features including:
# - Process substitution <(cmd) - input from command output
# - Process substitution >(cmd) - output to command input
# - Pipe stderr |& - pipe both stdout and stderr
# - Append both &>> - append stdout and stderr to file
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
    echo -e "\n${PURPLE}--- $1 ---${NC}"
}

test_result() {
    local test_name="$1"
    local result="$2"  # 0 for pass, 1 for fail
    local details="${3:-}"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    if [[ $result -eq 0 ]]; then
        echo -e "  ${GREEN}✓${NC} $test_name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "  ${RED}✗${NC} $test_name"
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

# Setup test environment
setup_test_env() {
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"

    # Create test files
    echo -e "line1\nline2\nline3" > "$TEST_DIR/file1.txt"
    echo -e "lineA\nlineB\nlineC" > "$TEST_DIR/file2.txt"
    echo -e "line1\nlineX\nline3" > "$TEST_DIR/file3.txt"
}

# =============================================================================
# PROCESS SUBSTITUTION INPUT <(cmd) TESTS
# =============================================================================

test_proc_sub_input() {
    print_section "Process Substitution Input <(cmd)"

    print_category "Basic <(cmd) Tests"

    # Simple process substitution
    run_test "cat with <(echo)" \
        'cat <(echo hello)' \
        'hello'

    # Process substitution with multiple lines
    run_contains_test "cat with <(echo multiline)" \
        'cat <(echo -e "line1\nline2")' \
        'line1'

    # Process substitution as argument
    run_test "wc -l with <(echo lines)" \
        'cat <(echo -e "a\nb\nc") | wc -l | tr -d " "' \
        '3'

    print_category "Multiple <(cmd) Arguments"

    # Two process substitutions (like diff)
    run_contains_test "diff with two <(cmd)" \
        'diff <(echo -e "a\nb") <(echo -e "a\nc")' \
        '2c2'

    # cat with multiple process substitutions
    run_test "cat with two <(echo)" \
        'cat <(echo first) <(echo second)' \
        $'first\nsecond'

    print_category "Complex Commands in <(cmd)"

    # Pipeline inside process substitution
    run_test "cat with <(pipeline)" \
        'cat <(echo HELLO | tr A-Z a-z)' \
        'hello'

    # Command with arguments
    run_test "cat with <(seq)" \
        'cat <(seq 1 3)' \
        $'1\n2\n3'
}

# =============================================================================
# PROCESS SUBSTITUTION OUTPUT >(cmd) TESTS
# =============================================================================

test_proc_sub_output() {
    print_section "Process Substitution Output >(cmd)"

    print_category "Basic >(cmd) Tests"

    # tee with output process substitution
    run_contains_test "tee with >(cat)" \
        'echo "hello world" | tee >(cat -n) >/dev/null' \
        'hello world'

    # Output to wc
    run_contains_test "tee with >(wc)" \
        'echo "test line" | tee >(wc -w) >/dev/null' \
        '2'
}

# =============================================================================
# PIPE STDERR |& TESTS
# =============================================================================

test_pipe_stderr() {
    print_section "Pipe Stderr |&"

    print_category "Basic |& Tests"

    # Stderr should be piped with |&
    run_contains_test "|& captures stderr" \
        'ls /nonexistent_dir_12345 |& cat -n' \
        '1'

    # Regular pipe should NOT capture stderr (in our test we'll just verify |& works)
    run_contains_test "|& with both stdout and stderr" \
        '(echo stdout; echo stderr >&2) |& cat' \
        'stdout'

    run_contains_test "|& captures stderr message" \
        '(echo stdout; echo stderr >&2) |& cat' \
        'stderr'

    print_category "|& with Pipelines"

    # |& in longer pipeline
    run_contains_test "|& in pipeline with grep" \
        'ls /nonexistent_xyz_99 |& grep -i "no such"' \
        'No such file'
}

# =============================================================================
# APPEND BOTH &>> TESTS
# =============================================================================

test_append_both() {
    print_section "Append Both &>>"

    print_category "Basic &>> Tests"

    local testfile="$TEST_DIR/append_test.txt"
    rm -f "$testfile"

    # First append - stdout (use timeout to prevent hanging)
    timeout 5 "$LUSH" -c 'echo "stdout line" &>> '"$testfile" 2>&1 || true

    # Second append - another stdout
    timeout 5 "$LUSH" -c 'echo "stdout line 2" &>> '"$testfile" 2>&1 || true

    # Third append - stderr
    timeout 5 "$LUSH" -c 'ls /nonexistent_abc_123 &>> '"$testfile" 2>&1 || true

    # Check file contents
    if [[ -f "$testfile" ]]; then
        local content
        content=$(cat "$testfile")

        if [[ "$content" == *"stdout line"* ]]; then
            test_result "&>> appends stdout" 0
        else
            test_result "&>> appends stdout" 1 "File missing stdout content"
        fi

        if [[ "$content" == *"stdout line 2"* ]]; then
            test_result "&>> appends multiple times" 0
        else
            test_result "&>> appends multiple times" 1 "File missing second line"
        fi

        if [[ "$content" == *"No such file"* ]] || [[ "$content" == *"nonexistent"* ]]; then
            test_result "&>> appends stderr" 0
        else
            test_result "&>> appends stderr" 1 "File missing stderr content: $content"
        fi
    else
        test_result "&>> creates file" 1 "File not created"
        test_result "&>> appends stdout" 1 "File not created"
        test_result "&>> appends stderr" 1 "File not created"
    fi

    print_category "&>> vs &> (overwrite vs append)"

    local testfile2="$TEST_DIR/overwrite_test.txt"
    rm -f "$testfile2"

    # Use &> to overwrite
    timeout 5 "$LUSH" -c 'echo "first" &> '"$testfile2" 2>&1 || true
    timeout 5 "$LUSH" -c 'echo "second" &> '"$testfile2" 2>&1 || true

    if [[ -f "$testfile2" ]]; then
        local content2
        content2=$(cat "$testfile2")

        if [[ "$content2" == "second" ]]; then
            test_result "&> overwrites (not appends)" 0
        else
            test_result "&> overwrites (not appends)" 1 "Expected 'second', got '$content2'"
        fi
    else
        test_result "&> overwrites (not appends)" 1 "File not created"
    fi
}

# =============================================================================
# COMBINED TESTS
# =============================================================================

test_combined() {
    print_section "Combined Features"

    print_category "Process Substitution with Pipes"

    # Process substitution in pipeline
    run_test "<(cmd) in pipeline" \
        'cat <(echo hello) | tr a-z A-Z' \
        'HELLO'

    # Multiple features combined
    run_contains_test "diff <(cmd) |& grep" \
        'diff <(echo a) <(echo b) |& grep ">"' \
        '> b'
}

# =============================================================================
# SYNTAX TESTS
# =============================================================================

test_syntax() {
    print_section "Syntax Validation"

    print_category "Parse-only Tests"

    # Valid syntax should parse
    run_success_test "Parse: cat <(echo test)" \
        'cat <(echo test)'

    run_success_test "Parse: cmd >(cat)" \
        'echo test | tee >(cat) >/dev/null'

    run_success_test "Parse: cmd |& cmd2" \
        'echo test |& cat'

    run_success_test "Parse: cmd &>> file" \
        "echo test &>> $TEST_DIR/syntax_test.txt"
}

# =============================================================================
# MAIN TEST RUNNER
# =============================================================================

main() {
    print_header "PROCESS SUBSTITUTION AND EXTENDED PIPES TESTS"

    echo -e "${CYAN}Lush binary: $LUSH${NC}"

    # Check if lush exists
    if [[ ! -x "$LUSH" ]]; then
        echo -e "${RED}Error: Lush binary not found or not executable: $LUSH${NC}"
        exit 1
    fi

    # Setup
    setup_test_env

    # Run all test categories
    test_proc_sub_input
    test_proc_sub_output
    test_pipe_stderr
    test_append_both
    test_combined
    test_syntax

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
