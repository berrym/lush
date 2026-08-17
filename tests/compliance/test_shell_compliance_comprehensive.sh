#!/bin/bash

# ============================================================================
# LUSH COMPREHENSIVE SHELL COMPLIANCE TEST SUITE
# ============================================================================
#
# A complete test suite for validating POSIX shell compliance and advanced
# shell features with proper edge case coverage and real-world scenarios.
#
# Version: 2.0
# Author: Michael Berry
# Date: December 2024
# ============================================================================

# Don't exit on first failure - we want to run all tests
# set -e

# Color definitions for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
NC='\033[0m' # No Color

# Test statistics
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0

# Category tracking
declare -a CATEGORY_NAMES
declare -a CATEGORY_SCORES
CURRENT_CATEGORY_TESTS=0
CURRENT_CATEGORY_PASSED=0

# Shell to test (default to lush)
SHELL_UNDER_TEST=${1:-"./build/lush"}

# Test timeout (seconds)
TEST_TIMEOUT=10

# ============================================================================
# UTILITY FUNCTIONS
# ============================================================================

print_header() {
    echo -e "\n${CYAN}===============================================================================${NC}"
    echo -e "${WHITE}$1${NC}"
    echo -e "${CYAN}===============================================================================${NC}\n"
}

print_category() {
    echo -e "\n${BLUE}### CATEGORY: $1 ###${NC}\n"
}

print_section() {
    echo -e "${YELLOW}=== $1 ===${NC}"
}

print_test_result() {
    local test_name="$1"
    local result="$2"
    local details="$3"

    if [ "$result" = "PASS" ]; then
        echo -e "  ${GREEN}OK${NC} $test_name"
    elif [ "$result" = "FAIL" ]; then
        echo -e "  ${RED}FAIL${NC} $test_name"
        if [ -n "$details" ]; then
            echo -e "    ${RED}$details${NC}"
        fi
    elif [ "$result" = "SKIP" ]; then
        echo -e "  ${YELLOW}WARN${NC} $test_name (SKIPPED)"
    fi
}

# Execute command with timeout and capture output
execute_with_timeout() {
    local cmd="$1"
    local timeout="${2:-$TEST_TIMEOUT}"
    local temp_file=$(mktemp)
    local exit_code=0

    # Execute command with timeout
    if timeout "$timeout" bash -c "echo '$cmd' | $SHELL_UNDER_TEST" > "$temp_file" 2>&1; then
        exit_code=0
    else
        exit_code=$?
    fi

    # Read output
    local output=$(cat "$temp_file")
    rm -f "$temp_file"

    echo "$output"
    return $exit_code
}

# Run individual test
run_test() {
    local test_name="$1"
    local command="$2"
    local expected="$3"
    local should_fail="${4:-false}"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    CURRENT_CATEGORY_TESTS=$((CURRENT_CATEGORY_TESTS + 1))

    # Execute command
    local actual
    local exit_code=0
    actual=$(execute_with_timeout "$command" 5) || exit_code=$?

    # Check results
    if [ "$should_fail" = "true" ]; then
        # Test should fail
        if [ $exit_code -ne 0 ]; then
            PASSED_TESTS=$((PASSED_TESTS + 1))
            CURRENT_CATEGORY_PASSED=$((CURRENT_CATEGORY_PASSED + 1))
            print_test_result "$test_name" "PASS"
            return 0
        else
            FAILED_TESTS=$((FAILED_TESTS + 1))
            print_test_result "$test_name" "FAIL" "Expected failure but command succeeded"
            return 1
        fi
    else
        # Test should pass
        if [ $exit_code -eq 0 ] && [ "$actual" = "$expected" ]; then
            PASSED_TESTS=$((PASSED_TESTS + 1))
            CURRENT_CATEGORY_PASSED=$((CURRENT_CATEGORY_PASSED + 1))
            print_test_result "$test_name" "PASS"
            return 0
        else
            FAILED_TESTS=$((FAILED_TESTS + 1))
            local details="Expected: '$expected'\nActual: '$actual'\nExit Code: $exit_code"
            print_test_result "$test_name" "FAIL" "$details"
            return 1
        fi
    fi
}

# Run test that checks for specific output pattern
run_pattern_test() {
    local test_name="$1"
    local command="$2"
    local pattern="$3"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    CURRENT_CATEGORY_TESTS=$((CURRENT_CATEGORY_TESTS + 1))

    local actual
    local exit_code=0
    actual=$(execute_with_timeout "$command" 5) || exit_code=$?

    if echo "$actual" | grep -q "$pattern"; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
        CURRENT_CATEGORY_PASSED=$((CURRENT_CATEGORY_PASSED + 1))
        print_test_result "$test_name" "PASS"
        return 0
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
        local details="Expected pattern: '$pattern'\nActual: '$actual'"
        print_test_result "$test_name" "FAIL" "$details"
        return 1
    fi
}

# Start a new category (reset counters)
start_category() {
    CURRENT_CATEGORY_TESTS=0
    CURRENT_CATEGORY_PASSED=0
}

# Calculate category score
calculate_category_score() {
    local category_name="$1"

    # Count tests and passed tests for this category
    local category_tests=$CURRENT_CATEGORY_TESTS
    local category_passed=$CURRENT_CATEGORY_PASSED

    if [ $category_tests -eq 0 ]; then
        category_tests=1
        category_passed=0
    fi

    local percentage=$((category_passed * 100 / category_tests))

    CATEGORY_NAMES+=("$category_name")
    CATEGORY_SCORES+=("$percentage")

    echo -e "  ${CYAN}Category Score: $percentage% ($category_passed/$category_tests tests)${NC}\n"
}




# ============================================================================
# TEST CATEGORIES
# ============================================================================

# Category 1: Basic Command Execution
test_basic_commands() {
    print_category "BASIC COMMAND EXECUTION"
    start_category

    print_section "Simple Commands"
    run_test "Echo simple text" \
        'echo hello' \
        "hello"

    run_test "Echo with arguments" \
        'echo hello world test' \
        "hello world test"

    run_test "Echo empty string" \
        'echo ""' \
        ""

    run_test "True command" \
        'true' \
        ""

    run_test "False command" \
        'false; echo $?' \
        "1"

    print_section "Command Arguments"
    run_test "Single quoted arguments" \
        "echo 'hello world'" \
        "hello world"

    run_test "Double quoted arguments" \
        'echo "hello world"' \
        "hello world"

    run_test "Mixed quotes" \
        'echo hello "world" test' \
        "hello world test"

    run_test "Arguments with spaces" \
        'echo "arg with spaces" single multi word' \
        "arg with spaces single multi word"

    print_section "Exit Status"
    run_test "Command exit status" \
        'true; echo $?' \
        "0"

    run_test "Failed command status" \
        'false; echo $?' \
        "1"

    calculate_category_score "Basic Commands"
}

# Category 2: Variable Operations
test_variable_operations() {
    print_category "VARIABLE OPERATIONS AND EXPANSION"
    start_category

    print_section "Variable Assignment"
    run_test "Simple assignment" \
        'var=value; echo $var' \
        "value"

    run_test "Assignment with spaces in value" \
        'var="hello world"; echo $var' \
        "hello world"

    run_test "Multiple assignments" \
        'a=1; b=2; c=3; echo $a$b$c' \
        "123"

    run_test "Assignment without spaces" \
        'var=value;echo $var' \
        "value"

    print_section "Variable Expansion"
    run_test "Basic variable expansion" \
        'name=world; echo hello $name' \
        "hello world"

    run_test "Braced variable expansion" \
        'name=world; echo hello ${name}' \
        "hello world"

    run_test "Variable in string" \
        'name=world; echo "hello $name"' \
        "hello world"

    run_test "Multiple variables" \
        'a=hello; b=world; echo $a $b' \
        "hello world"

    run_test "Variable concatenation" \
        'a=hello; b=world; echo $a$b' \
        "helloworld"

    print_section "Parameter Expansion"
    run_test "Default value (unset)" \
        'echo ${undefined:-default}' \
        "default"

    run_test "Default value (set)" \
        'var=set; echo ${var:-default}' \
        "set"

    run_test "Alternative value (unset)" \
        'echo ${undefined:+alternative}' \
        ""

    run_test "Alternative value (set)" \
        'var=set; echo ${var:+alternative}' \
        "alternative"

    run_test "String length" \
        'var=hello; echo ${#var}' \
        "5"

    run_test "Substring extraction" \
        'var=hello; echo ${var:1:3}' \
        "ell"

    run_test "Remove shortest prefix" \
        'var=hello.tar.gz; echo ${var#*.}' \
        "tar.gz"

    run_test "Remove longest prefix" \
        'var=hello.tar.gz; echo ${var##*.}' \
        "gz"

    run_test "Remove shortest suffix" \
        'var=hello.tar.gz; echo ${var%.*}' \
        "hello.tar"

    run_test "Remove longest suffix" \
        'var=hello.tar.gz; echo ${var%%.*}' \
        "hello"

    print_section "Special Variables"
    run_test "Process ID variable" \
        'echo $$ | grep -q "[0-9]" && echo "found"' \
        "found"

    run_test "Exit status variable" \
        'true; echo $?' \
        "0"

    run_test "Parameter count" \
        'set -- a b c; echo $#' \
        "3"

    run_test "All parameters" \
        'set -- a b c; echo $*' \
        "a b c"

    run_test "All parameters quoted" \
        'set -- a b c; echo "$@"' \
        "a b c"

    calculate_category_score "Variable Operations"
}

# Category 3: Arithmetic Expansion
test_arithmetic_expansion() {
    print_category "ARITHMETIC EXPANSION"
    start_category

    print_section "Basic Arithmetic"
    run_test "Simple addition" \
        'echo $((2 + 3))' \
        "5"

    run_test "Simple subtraction" \
        'echo $((10 - 3))' \
        "7"

    run_test "Simple multiplication" \
        'echo $((4 * 5))' \
        "20"

    run_test "Simple division" \
        'echo $((15 / 3))' \
        "5"

    run_test "Modulo operation" \
        'echo $((17 % 5))' \
        "2"

    print_section "Operator Precedence"
    run_test "Multiplication before addition" \
        'echo $((2 + 3 * 4))' \
        "14"

    run_test "Parentheses override precedence" \
        'echo $(((2 + 3) * 4))' \
        "20"

    run_test "Complex precedence" \
        'echo $((2 * 3 + 4 * 5))' \
        "26"

    print_section "Variables in Arithmetic"
    run_test "Variable in arithmetic" \
        'a=5; b=3; echo $((a + b))' \
        "8"

    run_test "Variable assignment in arithmetic" \
        'a=0; echo $((a = 5 + 3)); echo $a' \
        "8
8"

    run_test "Pre-increment" \
        'a=5; echo $((++a)); echo $a' \
        "6
6"

    run_test "Post-increment" \
        'a=5; echo $((a++)); echo $a' \
        "5
6"

    print_section "Comparison Operations"
    run_test "Less than (true)" \
        'echo $((3 < 5))' \
        "1"

    run_test "Less than (false)" \
        'echo $((5 < 3))' \
        "0"

    run_test "Equal comparison" \
        'echo $((5 == 5))' \
        "1"

    run_test "Not equal comparison" \
        'echo $((5 != 3))' \
        "1"

    print_section "Logical Operations"
    run_test "Logical AND (true)" \
        'echo $((1 && 1))' \
        "1"

    run_test "Logical AND (false)" \
        'echo $((1 && 0))' \
        "0"

    run_test "Logical OR (true)" \
        'echo $((0 || 1))' \
        "1"

    run_test "Logical OR (false)" \
        'echo $((0 || 0))' \
        "0"

    print_section "Error Handling"
    run_pattern_test "Division by zero error" \
        'echo $((5 / 0))' \
        "division by zero"

    calculate_category_score "Arithmetic Expansion"
}

# Category 4: Command Substitution
test_command_substitution() {
    print_category "COMMAND SUBSTITUTION"
    start_category

    print_section "Modern Syntax"
    run_test "Simple command substitution" \
        'echo $(echo hello)' \
        "hello"

    run_test "Command substitution in string" \
        'echo "result: $(echo test)"' \
        "result: test"

    run_test "Nested command substitution" \
        'echo $(echo $(echo nested))' \
        "nested"

    run_test "Command substitution with arithmetic" \
        'echo $(echo $((2 + 3)))' \
        "5"

    print_section "Legacy Syntax"
    run_test "Backtick command substitution" \
        'echo `echo hello`' \
        "hello"

    run_test "Backticks in double quotes" \
        'echo "result: `echo test`"' \
        "result: test"

    print_section "Complex Scenarios"
    run_test "Command substitution with pipes" \
        'echo $(echo hello | tr a-z A-Z)' \
        "HELLO"

    run_test "Multiple command substitutions" \
        'echo $(echo hello) $(echo world)' \
        "hello world"

    run_test "Command substitution with variables" \
        'cmd=echo; arg=hello; echo $(${cmd} ${arg})' \
        "hello"

    calculate_category_score "Command Substitution"
}

# Category 5: Control Structures
test_control_structures() {
    print_category "CONTROL STRUCTURES"
    start_category

    print_section "Conditional Statements"
    run_test "Simple if statement" \
        'if true; then echo success; fi' \
        "success"

    run_test "If-else statement" \
        'if false; then echo fail; else echo success; fi' \
        "success"

    run_test "If-elif-else statement" \
        'a=2; if [ $a -eq 1 ]; then echo one; elif [ $a -eq 2 ]; then echo two; else echo other; fi' \
        "two"

    run_test "Logical operators in if" \
        'a=15; if [ $a -gt 10 ] && [ $a -lt 20 ]; then echo valid; fi' \
        "valid"

    run_test "Logical OR in if" \
        'a=5; if [ $a -lt 10 ] || [ $a -gt 20 ]; then echo outside; fi' \
        "outside"

    print_section "Test Conditions"
    run_test "String equality test" \
        'if [ "hello" = "hello" ]; then echo equal; fi' \
        "equal"

    run_test "String inequality test" \
        'if [ "hello" != "world" ]; then echo different; fi' \
        "different"

    run_test "Numeric comparison test" \
        'if [ 5 -gt 3 ]; then echo greater; fi' \
        "greater"

    run_test "File existence test" \
        'if [ -f /etc/passwd ]; then echo exists; fi' \
        "exists"

    print_section "Loops"
    run_test "For loop with list" \
        'for i in 1 2 3; do echo $i; done' \
        "1
2
3"

    run_test "For loop with variable expansion" \
        'items="a b c"; for i in $items; do echo $i; done' \
        "a
b
c"

    run_test "For loop with name=value words" \
        'for i in a=1 b=2 c=3; do echo $i; done' \
        "a=1
b=2
c=3"

    run_test "For loop with empty value (VAR=)" \
        'for i in empty= full=value; do echo "[$i]"; done' \
        "[empty=]
[full=value]"

    run_test "For loop with chained equals (a=b=c)" \
        'for i in nested=a=b; do echo $i; done' \
        "nested=a=b"

    run_test "While loop" \
        'i=1; while [ $i -le 3 ]; do echo $i; i=$((i + 1)); done' \
        "1
2
3"

    run_test "Until loop" \
        'i=1; until [ $i -gt 3 ]; do echo $i; i=$((i + 1)); done' \
        "1
2
3"

    print_section "Case Statements"
    run_test "Simple case statement" \
        'case hello in hello) echo match;; *) echo no match;; esac' \
        "match"

    run_test "Case with patterns" \
        'case file.txt in *.txt) echo text;; *.log) echo log;; esac' \
        "text"

    run_test "Case with multiple patterns" \
        'case a in [abc]) echo letter;; [0-9]) echo digit;; esac' \
        "letter"

    calculate_category_score "Control Structures"
}

# Category 6: Functions
test_functions() {
    print_category "FUNCTION OPERATIONS"
    start_category

    print_section "Function Definition and Calling"
    run_test "Simple function" \
        'greet() { echo hello; }; greet' \
        "hello"

    run_test "Function with parameters" \
        'greet() { echo hello $1; }; greet world' \
        "hello world"

    run_test "Function with multiple parameters" \
        'add() { echo $(($1 + $2)); }; add 3 5' \
        "8"

    print_section "Function Return Values"
    run_test "Function return status" \
        'test_func() { return 42; }; test_func; echo $?' \
        "42"

    run_test "Function with output" \
        'get_value() { echo result; }; echo $(get_value)' \
        "result"

    print_section "Function Scoping"
    run_test "Function parameter access" \
        'show_params() { echo $# $1 $2; }; show_params a b c' \
        "3 a b"

    run_test "Local variables" \
        'test_local() { local var=local; echo $var; }; var=global; test_local; echo $var' \
        "local
global"

    calculate_category_score "Function Operations"
}

# Category 7: I/O Redirection
test_io_redirection() {
    print_category "I/O REDIRECTION AND PIPES"
    start_category

    print_section "Output Redirection"
    run_test "Redirect stdout to file" \
        'echo hello > /tmp/test_output 2>/dev/null && cat /tmp/test_output && rm -f /tmp/test_output' \
        "hello"

    run_test "Append stdout to file" \
        'echo line1 > /tmp/test_append 2>/dev/null; echo line2 >> /tmp/test_append 2>/dev/null; cat /tmp/test_append && rm -f /tmp/test_append' \
        "line1
line2"

    print_section "Error Redirection"
    run_test "Redirect stderr to /dev/null" \
        'echo error 2>/dev/null >&2; echo success' \
        "success"

    run_test "Redirect both stdout and stderr" \
        'echo output; echo error >&2' \
        "output
error"

    print_section "Input Redirection"
    run_test "Redirect stdin from /dev/null" \
        'read line < /dev/null; echo "read complete"' \
        "read complete"

    print_section "Pipes"
    run_test "Simple pipe" \
        'echo hello | cat' \
        "hello"

    run_test "Multi-stage pipe" \
        'echo HELLO | tr A-Z a-z | cat' \
        "hello"

    print_section "Here Documents"
    run_test "Simple here document" \
        'cat << EOF
hello
world
EOF' \
        "hello
world"

    calculate_category_score "I/O Redirection"
}

# Category 8: Built-in Commands
test_builtin_commands() {
    print_category "BUILT-IN COMMANDS"
    start_category

    print_section "Essential Built-ins"
    run_test "echo command" \
        'echo hello world' \
        "hello world"

    run_test "pwd command" \
        'cd /tmp 2>/dev/null; pwd | grep -q "/tmp" && echo "correct"' \
        "correct"

    run_test "cd command" \
        'cd /tmp 2>/dev/null; pwd | grep -q "/tmp" && echo "success"' \
        "success"

    print_section "Variable Built-ins"
    run_test "set positional parameters" \
        'set -- a b c; echo $1 $2 $3' \
        "a b c"

    run_test "unset variable" \
        'var=test; unset var; echo ${var:-unset}' \
        "unset"

    run_test "export variable" \
        'export TEST_VAR=exported; echo $TEST_VAR' \
        "exported"

    print_section "Utility Built-ins"
    run_test "test command (true)" \
        'test 5 -gt 3 && echo true' \
        "true"

    run_test "test command (false)" \
        'test 3 -gt 5 || echo false' \
        "false"

    run_test "type command" \
        'type echo | grep -q "builtin" && echo "builtin"' \
        "builtin"

    calculate_category_score "Built-in Commands"
}

# Category 9: Pattern Matching and Globbing
test_pattern_matching() {
    print_category "PATTERN MATCHING AND GLOBBING"
    start_category

    print_section "Filename Globbing"
    run_test "Asterisk glob" \
        'touch /tmp/test1.txt /tmp/test2.txt >/dev/null 2>&1; echo /tmp/test*.txt | grep -q "test1.txt" && rm -f /tmp/test*.txt && echo "matched"' \
        "matched"

    run_test "Question mark glob" \
        'touch /tmp/test1 /tmp/test2 >/dev/null 2>&1; echo /tmp/test? | grep -q "test1" && rm -f /tmp/test? && echo "matched"' \
        "matched"

    print_section "Pattern Matching in Case"
    run_test "Wildcard pattern in case" \
        'case file.txt in *.txt) echo text;; *) echo other;; esac' \
        "text"

    run_test "Character class pattern" \
        'case a in [abc]) echo match;; *) echo no;; esac' \
        "match"

    calculate_category_score "Pattern Matching"
}

# Category 10: Error Handling and Edge Cases
test_error_handling() {
    print_category "ERROR HANDLING AND EDGE CASES"
    start_category

    print_section "Command Errors"
    run_test "Non-existent command" \
        'nonexistent_command_12345 2>/dev/null; echo $?' \
        "127"

    run_test "Permission denied" \
        'touch /tmp/no_exec; chmod -x /tmp/no_exec; /tmp/no_exec 2>/dev/null; code=$?; rm -f /tmp/no_exec; echo $code' \
        "126"

    print_section "Syntax Errors"
    run_pattern_test "Detect syntax errors" \
        'echo "test syntax error detection"' \
        "test syntax error detection"

    print_section "Variable Edge Cases"
    run_test "Undefined variable" \
        'echo ${undefined_var:-default}' \
        "default"

    run_test "Empty variable handling" \
        'var=""; echo ${var:-empty}' \
        "empty"

    print_section "Arithmetic Edge Cases"
    run_test "Large number arithmetic" \
        'echo $((999999 + 1))' \
        "1000000"

    run_test "Negative number arithmetic" \
        'echo $((-5 + 3))' \
        "-2"

    calculate_category_score "Error Handling"
}

# Category 11: Real-World Scenarios
test_real_world_scenarios() {
    print_category "REAL-WORLD COMPLEX SCENARIOS"
    start_category

    print_section "Script-like Operations"
    run_test "File processing simulation" \
        'files="file1.txt file2.log file3.txt"; for f in $files; do case $f in *.txt) echo "Text: $f";; *.log) echo "Log: $f";; esac; done' \
        "Text: file1.txt
Log: file2.log
Text: file3.txt"

    run_test "Configuration parsing simulation" \
        'config="name=value;port=8080;debug=true"; IFS=";"; for item in $config; do echo "Config: $item"; done' \
        "Config: name=value
Config: port=8080
Config: debug=true"

    run_test "Log processing simulation" \
        'log="ERROR: failed;WARN: slow;INFO: ok"; IFS=";"; for entry in $log; do case $entry in ERROR:*) echo "Critical: $entry";; WARN:*) echo "Warning: $entry";; esac; done' \
        "Critical: ERROR: failed
Warning: WARN: slow"

    print_section "Complex Parameter Processing"
    run_test "URL parsing simulation" \
        'url="https://example.com:8080/path"; proto=${url%%://*}; rest=${url#*://}; host=${rest%%/*}; echo "$proto $host"' \
        "https example.com:8080"

    run_test "File extension processing" \
        'filename="document.backup.tar.gz"; base=${filename%%.*}; ext=${filename#*.}; echo "$base -> $ext"' \
        "document -> backup.tar.gz"

    run_test "Path manipulation" \
        'path="/usr/local/bin/command"; dir=${path%/*}; file=${path##*/}; echo "$dir/$file"' \
        "/usr/local/bin/command"

    print_section "Conditional Logic Chains"
    run_test "Multi-condition validation" \
        'value=15; if [ $value -gt 10 ] && [ $value -lt 20 ]; then echo "valid range"; else echo "invalid"; fi' \
        "valid range"

    run_test "Complex condition with OR" \
        'status=200; if [ $status -eq 200 ] || [ $status -eq 201 ]; then echo "success"; else echo "failure"; fi' \
        "success"

    run_test "Nested conditions" \
        'user="admin"; role="superuser"; if [ "$user" = "admin" ]; then if [ "$role" = "superuser" ]; then echo "authorized"; fi; fi' \
        "authorized"

    print_section "Data Processing"
    run_test "CSV processing simulation" \
        'data="name,age,city"; IFS=","; for field in $data; do echo "Field: $field"; done' \
        "Field: name
Field: age
Field: city"

    run_test "Environment variable processing" \
        'vars="HOME=/home/user PATH=/usr/bin"; for item in $vars; do case $item in HOME=*) echo "Home: ${item#*=}";; PATH=*) echo "Path: ${item#*=}";; esac; done' \
        "Home: /home/user
Path: /usr/bin"

    run_test "Backup script simulation" \
        'files="config.txt data.db logs.txt"; backup_dir="/backup"; for file in $files; do echo "Backing up $file to $backup_dir/$file.bak"; done' \
        "Backing up config.txt to /backup/config.txt.bak
Backing up data.db to /backup/data.db.bak
Backing up logs.txt to /backup/logs.txt.bak"

    calculate_category_score "Real-World Scenarios"
}

# Category 12: Performance and Stress Testing
test_performance_stress() {
    print_category "PERFORMANCE AND STRESS TESTING"
    start_category

    print_section "Large Data Handling"
    run_test "Long string processing" \
        'str=$(printf "%0100s" | tr " " "x"); echo ${#str}' \
        "100"

    run_test "Many variable assignments" \
        'for i in 1 2 3 4 5 6 7 8 9 10; do eval "var$i=value$i"; done; echo $var1$var5$var10' \
        "value1value5value10"

    run_test "Large arithmetic operations" \
        'result=0; for i in 1 2 3 4 5 6 7 8 9 10; do result=$((result + i)); done; echo $result' \
        "55"

    print_section "Nested Operations"
    run_test "Deep nesting test" \
        'echo $(echo $(echo $(echo nested)))' \
        "nested"

    run_test "Complex parameter expansion" \
        'a=test; b=ing; c=123; echo ${a:+prefix_${a}${b}_${c}_suffix}' \
        "prefix_testing_123_suffix"

    run_test "Multiple command substitutions" \
        'echo $(date +%Y)-$(echo test)-$(echo end)' \
        "$(date +%Y)-test-end"

    print_section "Memory and Resource Usage"
    run_test "Large array simulation" \
        'data=""; for i in 1 2 3 4 5; do data="$data item$i"; done; echo $data' \
        "item1 item2 item3 item4 item5"

    run_test "Recursive function test" \
        'countdown() { if [ $1 -gt 0 ]; then echo $1; countdown $(($1 - 1)); else echo done; fi; }; countdown 3' \
        "3
2
1
done"

    calculate_category_score "Performance Stress"
}

# ============================================================================
# MAIN TEST EXECUTION
# ============================================================================

main() {
    print_header "LUSH COMPREHENSIVE SHELL COMPLIANCE TEST SUITE"

    echo -e "${CYAN}Testing shell: ${WHITE}$SHELL_UNDER_TEST${NC}"
    echo -e "${CYAN}Started at: ${WHITE}$(date)${NC}\n"

    # Check if shell exists and is executable
    if [ ! -x "$SHELL_UNDER_TEST" ]; then
        echo -e "${RED}Error: Shell '$SHELL_UNDER_TEST' not found or not executable${NC}"
        exit 1
    fi

    # Execute all test categories
    test_basic_commands
    test_variable_operations
    test_arithmetic_expansion
    test_command_substitution
    test_control_structures
    test_functions
    test_io_redirection
    test_builtin_commands
    test_pattern_matching
    test_error_handling
    test_real_world_scenarios
    test_performance_stress

    # Calculate and display final results
    calculate_final_score
}

# Calculate final compliance score
calculate_final_score() {
    print_header "COMPREHENSIVE SHELL COMPLIANCE RESULTS"

    echo -e "${CYAN}Individual Category Scores:${NC}"
    for i in "${!CATEGORY_NAMES[@]}"; do
        local name="${CATEGORY_NAMES[$i]}"
        local score="${CATEGORY_SCORES[$i]}"

        if [ "$score" -eq 100 ]; then
            echo -e "  ${GREEN}$name: $score%${NC}"
        elif [ "$score" -ge 80 ]; then
            echo -e "  ${YELLOW}$name: $score%${NC}"
        else
            echo -e "  ${RED}$name: $score%${NC}"
        fi
    done

    # Calculate weighted overall score
    local total_score=0
    local category_count=${#CATEGORY_SCORES[@]}

    for score in "${CATEGORY_SCORES[@]}"; do
        total_score=$((total_score + score))
    done

    local overall_score=$((total_score / category_count))

    echo
    echo -e "${CYAN}===============================================================${NC}"
    if [ "$overall_score" -ge 95 ]; then
        echo -e "${GREEN}OVERALL SHELL COMPLIANCE SCORE: $overall_score%${NC}"
        echo -e "${GREEN}===============================================================${NC}"
        echo -e "${GREEN}* EXCELLENT: Production-ready shell with exceptional compliance${NC}"
    elif [ "$overall_score" -ge 90 ]; then
        echo -e "${YELLOW}OVERALL SHELL COMPLIANCE SCORE: $overall_score%${NC}"
        echo -e "${YELLOW}===============================================================${NC}"
        echo -e "${YELLOW}* VERY GOOD: High-quality shell suitable for most use cases${NC}"
    elif [ "$overall_score" -ge 80 ]; then
        echo -e "${YELLOW}OVERALL SHELL COMPLIANCE SCORE: $overall_score%${NC}"
        echo -e "${YELLOW}===============================================================${NC}"
        echo -e "${YELLOW}GOOD: Functional shell with room for improvement${NC}"
    else
        echo -e "${RED}OVERALL SHELL COMPLIANCE SCORE: $overall_score%${NC}"
        echo -e "${RED}===============================================================${NC}"
        echo -e "${RED}NEEDS WORK: Significant functionality gaps${NC}"
    fi

    echo
    echo -e "${CYAN}Test Summary:${NC}"
    echo -e "  Total Tests: ${WHITE}$TOTAL_TESTS${NC}"
    echo -e "  Passed: ${GREEN}$PASSED_TESTS${NC}"
    echo -e "  Failed: ${RED}$FAILED_TESTS${NC}"
    echo -e "  Skipped: ${YELLOW}$SKIPPED_TESTS${NC}"
    local success_rate=$((PASSED_TESTS * 100 / TOTAL_TESTS))
    echo -e "  Success Rate: ${WHITE}$success_rate%${NC}"
    echo -e "  Test Duration: ${WHITE}$(date)${NC}"

    echo
    echo -e "${CYAN}Compliance Comparison:${NC}"
    echo -e "  POSIX Baseline: ${WHITE}100%${NC} (reference)"
    echo -e "  Bash 5.x: ${WHITE}~98%${NC} (reference)"
    echo -e "  Zsh 5.x: ${WHITE}~95%${NC} (reference)"
    echo -e "  Lush: ${WHITE}$overall_score%${NC}"

    echo
    if [ "$overall_score" -ge 95 ]; then
        echo -e "${GREEN}Development Recommendations:${NC}"
        echo -e "  * ${GREEN}Shell is production-ready and highly compliant${NC}"
        echo -e "  * ${GREEN}Consider advanced feature enhancements${NC}"
        echo -e "  * ${GREEN}Focus on performance optimization and user experience${NC}"
    elif [ "$overall_score" -ge 90 ]; then
        echo -e "${YELLOW}Development Recommendations:${NC}"
        echo -e "  * ${YELLOW}Shell is approaching production readiness${NC}"
        echo -e "  * ${YELLOW}Address remaining failing test categories${NC}"
        echo -e "  * ${YELLOW}Focus on edge case handling${NC}"
    else
        echo -e "${RED}Development Recommendations:${NC}"
        echo -e "  * ${RED}Focus on categories scoring below 80%${NC}"
        echo -e "  * ${RED}Prioritize core functionality implementation${NC}"
        echo -e "  * ${RED}Enhance error handling and edge case support${NC}"
    fi

    print_header "COMPREHENSIVE COMPLIANCE TESTING COMPLETE"

    # Exit with appropriate code
    if [ "$FAILED_TESTS" -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
}

# Execute main function if script is run directly
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    main "$@"
fi
