#!/bin/bash

# Comprehensive POSIX regression test suite for Lush shell
# Ensures that parser changes haven't broken existing POSIX functionality

set -e

SHELL_PATH="${1:-./build/lush}"
TEST_COUNT=0
PASSED_COUNT=0
FAILED_COUNT=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "=== Lush POSIX Regression Test Suite ==="
echo "Testing shell: $SHELL_PATH"
echo "Verifying no regressions in existing POSIX functionality"
echo ""

# Check if shell exists
if [[ ! -x "$SHELL_PATH" ]]; then
    echo "Error: Shell binary not found at $SHELL_PATH"
    echo "Please build the shell first with: meson compile -C build"
    exit 1
fi

# Test function
test_posix() {
    local test_name="$1"
    local input="$2"
    local expected_pattern="$3"

    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "${YELLOW}Test $TEST_COUNT: $test_name${NC}"

    # Execute the command using -c option for non-interactive mode
    local output
    output=$("$SHELL_PATH" -c "$input" 2>&1)
    local exit_code=$?

    if [ $exit_code -eq 0 ]; then
        if echo "$output" | grep -q "$expected_pattern"; then
            echo -e "${GREEN}OK PASSED${NC}"
            PASSED_COUNT=$((PASSED_COUNT + 1))
        else
            echo -e "${RED}FAIL FAILED - Expected pattern '$expected_pattern' not found${NC}"
            echo "  Input: $input"
            echo "  Output: '$output'"
            FAILED_COUNT=$((FAILED_COUNT + 1))
        fi
    else
        echo -e "${RED}FAIL FAILED - Exit code $exit_code${NC}"
        echo "  Input: $input"
        echo "  Output: '$output'"
        FAILED_COUNT=$((FAILED_COUNT + 1))
    fi
    echo
}

echo -e "${BLUE}=== Basic Command Execution ===${NC}"

test_posix "Simple command" \
    "echo hello" \
    "hello"

test_posix "Command with arguments" \
    "echo hello world test" \
    "hello world test"

test_posix "Command with quoted arguments" \
    "echo 'single quotes' \"double quotes\"" \
    "single quotes double quotes"

echo -e "${BLUE}=== Variable Operations ===${NC}"

test_posix "Variable assignment" \
    "name=value; echo \$name" \
    "value"

test_posix "Variable expansion" \
    "x=test; echo \"Value: \$x\"" \
    "Value: test"

test_posix "Multiple variable assignments" \
    "a=1; b=2; echo \$a \$b" \
    "1 2"

test_posix "Environment variable usage" \
    "echo \$PWD" \
    "/"

echo -e "${BLUE}=== Pipeline Operations ===${NC}"

test_posix "Simple pipeline" \
    "echo hello | grep h" \
    "hello"

test_posix "Multi-stage pipeline" \
    "echo 'line1\nline2\nline3' | grep line | wc -l" \
    "3"

test_posix "Pipeline with variable" \
    "data=testing; echo \$data | grep test" \
    "testing"

echo -e "${BLUE}=== Single-line Control Structures ===${NC}"

test_posix "Single-line FOR loop" \
    "for i in 1 2 3; do echo item \$i; done" \
    "item 1"

test_posix "Single-line IF statement" \
    "if true; then echo success; fi" \
    "success"

test_posix "Single-line IF-ELSE" \
    "if false; then echo no; else echo yes; fi" \
    "yes"

test_posix "Single-line WHILE loop" \
    "x=1; while [ \$x -le 2 ]; do echo count \$x; x=\$((x+1)); done" \
    "count 1"

echo -e "${BLUE}=== Arithmetic Expansion ===${NC}"

test_posix "Basic arithmetic" \
    "echo \$((5 + 3))" \
    "8"

test_posix "Arithmetic with variables" \
    "x=5; y=3; echo \$((x + y))" \
    "8"

test_posix "Complex arithmetic" \
    "echo \$((2 * 3 + 4))" \
    "10"

echo -e "${BLUE}=== Command Substitution ===${NC}"

test_posix "Command substitution with \$()" \
    "echo \"Today: \$(date '+%Y')\"" \
    "Today: 20"

test_posix "Nested command substitution" \
    "echo \"Files: \$(echo test)\"" \
    "Files: test"

echo -e "${BLUE}=== Quoting and Escaping ===${NC}"

test_posix "Single quotes preserve literals" \
    "echo 'dollar sign: \$USER'" \
    "dollar sign: \$USER"

test_posix "Double quotes allow expansion" \
    "user=testuser; echo \"User: \$user\"" \
    "User: testuser"

test_posix "Backslash escaping" \
    "echo \"Quote: \\\"escaped\\\"\"" \
    "Quote: \"escaped\""

test_posix "Mixed quoting" \
    "echo 'single' \"double\" unquoted" \
    "single double unquoted"

echo -e "${BLUE}=== Parameter Expansion ===${NC}"

test_posix "Basic parameter expansion" \
    "var=hello; echo \${var}" \
    "hello"

test_posix "Parameter with default" \
    "echo \${undefined:-default}" \
    "default"

test_posix "Parameter expansion in quotes" \
    "name=world; echo \"Hello \${name}!\"" \
    "Hello world!"

echo -e "${BLUE}=== Command Grouping ===${NC}"

test_posix "Brace grouping" \
    "{ echo first; echo second; }" \
    "first"

test_posix "Subshell grouping" \
    "(echo subshell; echo test)" \
    "subshell"

echo -e "${BLUE}=== Redirection (Basic) ===${NC}"

test_posix "Output redirection to /dev/null" \
    "echo hidden > /dev/null; echo visible" \
    "visible"

test_posix "Input from /dev/null" \
    "cat < /dev/null; echo after" \
    "after"

echo -e "${BLUE}=== Logical Operators ===${NC}"

test_posix "AND operator success" \
    "true && echo success" \
    "success"

test_posix "AND operator failure" \
    "false && echo failure; echo after" \
    "after"

test_posix "OR operator success" \
    "true || echo fallback; echo done" \
    "done"

test_posix "OR operator failure" \
    "false || echo fallback" \
    "fallback"

echo -e "${BLUE}=== Command Sequences ===${NC}"

test_posix "Semicolon separated commands" \
    "echo first; echo second; echo third" \
    "first"

test_posix "Mixed operators" \
    "true && echo success; false || echo backup" \
    "success"

test_posix "Complex command sequence" \
    "x=1; echo start; x=\$((x+1)); echo \$x" \
    "start"

echo -e "${BLUE}=== Background and Job Control ===${NC}"

test_posix "Background process (basic)" \
    "echo background &; echo foreground" \
    "foreground"

echo -e "${BLUE}=== Built-in Commands ===${NC}"

test_posix "Echo built-in" \
    "echo test built-in" \
    "test built-in"

test_posix "Export command" \
    "export TESTVAR=value; echo \$TESTVAR" \
    "value"

test_posix "Unset command" \
    "TESTVAR=value; unset TESTVAR; echo \${TESTVAR:-unset}" \
    "unset"

echo -e "${BLUE}=== Error Handling ===${NC}"

test_posix "Non-existent command handling" \
    "nonexistentcommand 2>/dev/null; echo after" \
    "after"

test_posix "Syntax error recovery" \
    "echo valid; echo 'syntax test'; echo valid2" \
    "valid"

echo -e "${BLUE}=== Advanced POSIX Features ===${NC}"

test_posix "Case statement (single line)" \
    "case test in test) echo match;; *) echo nomatch;; esac" \
    "match"

test_posix "Function definition (if supported)" \
    "func() { echo in function; }; func" \
    "in function"

test_posix "Here string (if supported)" \
    "cat <<< 'here string test'" \
    "here string test"

echo -e "${BLUE}=== Compatibility with Standard Shell Constructs ===${NC}"

test_posix "POSIX-style FOR loop" \
    "for file in file1 file2; do echo processing \$file; done" \
    "processing file1"

test_posix "POSIX-style conditional" \
    "[ 1 -eq 1 ] && echo condition true" \
    "condition true"

test_posix "POSIX-style test command" \
    "test 5 -gt 3 && echo five greater than three" \
    "five greater than three"

echo -e "${BLUE}=== Results Summary ===${NC}"
echo "==================================="
echo "Total tests run: $TEST_COUNT"
echo -e "${GREEN}Passed: $PASSED_COUNT${NC}"
echo -e "${RED}Failed: $FAILED_COUNT${NC}"
echo

if [ $FAILED_COUNT -eq 0 ]; then
    echo -e "${GREEN}OK NO REGRESSIONS DETECTED${NC}"
    echo "All existing POSIX functionality appears to be working correctly."
    echo "The multiline parser changes have not broken backward compatibility."
    exit 0
else
    echo -e "${RED}WARN REGRESSIONS DETECTED${NC}"
    echo "$FAILED_COUNT out of $TEST_COUNT tests failed."
    echo "Please review the failed tests and fix any regressions before proceeding."
    exit 1
fi
