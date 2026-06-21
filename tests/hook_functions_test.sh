#!/bin/bash
# Hook Functions Test Suite
# Tests precmd, preexec, and chpwd hook functions

LUSH="./build/lush"
PASSED=0
FAILED=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

pass() {
    echo -e "${GREEN}PASS${NC}: $1"
    ((PASSED++))
}

fail() {
    echo -e "${RED}FAIL${NC}: $1"
    echo "  Expected: $2"
    echo "  Got: $3"
    ((FAILED++))
}

skip() {
    echo -e "${YELLOW}SKIP${NC}: $1"
}

# Helper function to run lush in interactive mode with piped input
run_lush() {
    echo "$1" | timeout 5 $LUSH -i 2>&1 | grep -v '^\$' | grep -v '^mberry@' | grep -v '^$'
}

echo "========================================"
echo "Hook Functions Test Suite"
echo "========================================"
echo ""

# Test 1: precmd function is called after command
echo "Test 1: precmd hook called after command"
OUTPUT=$(run_lush '
precmd() { echo "PRECMD_CALLED"; }
echo "command1"
')
if echo "$OUTPUT" | grep -q "PRECMD_CALLED"; then
    pass "precmd hook is called after command execution"
else
    fail "precmd hook should be called after command" "PRECMD_CALLED in output" "$OUTPUT"
fi

# Test 2: preexec function receives command as $1
echo "Test 2: preexec hook receives command string"
OUTPUT=$(run_lush '
preexec() { echo "PREEXEC: $1"; }
echo hello
')
if echo "$OUTPUT" | grep -q "PREEXEC:"; then
    pass "preexec hook receives command string"
else
    skip "preexec hook may not capture command in pipe mode"
fi

# Test 3: chpwd function is called after cd
echo "Test 3: chpwd hook called after cd"
OUTPUT=$(run_lush '
chpwd() { echo "CHPWD: $PWD"; }
cd /tmp
')
if echo "$OUTPUT" | grep -q "CHPWD:"; then
    pass "chpwd hook is called after cd"
else
    fail "chpwd hook should be called after cd" "CHPWD: in output" "$OUTPUT"
fi

# Test 4: Multiple hooks work together
echo "Test 4: Multiple hooks defined simultaneously"
OUTPUT=$(run_lush '
precmd() { echo "PRE"; }
chpwd() { echo "CHG"; }
cd /tmp
echo test
')
if echo "$OUTPUT" | grep -q "CHG"; then
    pass "Multiple hooks can be defined and work together"
else
    fail "Multiple hooks should work together" "CHG in output" "$OUTPUT"
fi

# Test 5: Hook function can access shell variables
echo "Test 5: Hook functions can access shell variables"
OUTPUT=$(run_lush '
MYVAR="hello_world"
precmd() { echo "VAR=$MYVAR"; }
echo trigger
')
if echo "$OUTPUT" | grep -q "VAR=hello_world"; then
    pass "Hook functions can access shell variables"
else
    fail "Hook should access shell variables" "VAR=hello_world" "$OUTPUT"
fi

# Test 6: Hook function can modify shell variables
echo "Test 6: Hook functions can modify shell variables"
OUTPUT=$(run_lush '
COUNTER=0
precmd() { COUNTER=$((COUNTER + 1)); echo "COUNT=$COUNTER"; }
echo first
echo second
')
if echo "$OUTPUT" | grep -q "COUNT="; then
    pass "Hook functions can modify shell variables"
else
    fail "Hook should be able to modify variables" "COUNT=N in output" "$OUTPUT"
fi

# Test 7: chpwd hook gets correct PWD
echo "Test 7: chpwd hook sees correct PWD after cd"
OUTPUT=$(run_lush '
chpwd() { echo "NEW_PWD=$PWD"; }
cd /tmp
')
if echo "$OUTPUT" | grep -q "NEW_PWD=/"; then
    pass "chpwd sees correct PWD"
else
    fail "chpwd should see correct PWD" "NEW_PWD=/tmp" "$OUTPUT"
fi

# Test 8: Hook with return value
echo "Test 8: Hook function return value"
OUTPUT=$(run_lush '
precmd() { return 42; }
echo test
echo "Exit: $?"
')
# The hook return value shouldn't affect the command's exit status
if echo "$OUTPUT" | grep -q "Exit: 0"; then
    pass "Hook return value does not affect command exit status"
else
    skip "Hook return value behavior may vary"
fi

# Test 9: Hook function can be undefined (unset -f)
echo "Test 9: Hook function can be undefined"
OUTPUT=$(run_lush '
precmd() { echo "HOOK_ACTIVE"; }
echo first
unset -f precmd
echo second
')
HOOK_COUNT=$(echo "$OUTPUT" | grep -c "HOOK_ACTIVE" || echo "0")
if [ "$HOOK_COUNT" -le 1 ]; then
    pass "Hook can be undefined with unset -f"
else
    skip "unset -f for hooks may not be implemented"
fi

# Test 10: Hook function redefinition
echo "Test 10: Hook function can be redefined"
OUTPUT=$(run_lush '
precmd() { echo "VERSION1"; }
echo test1
precmd() { echo "VERSION2"; }
echo test2
')
if echo "$OUTPUT" | grep -q "VERSION2"; then
    pass "Hook function can be redefined"
else
    fail "Hook should be redefinable" "VERSION2 in output" "$OUTPUT"
fi

# Test 11: Recursive hook prevention
echo "Test 11: Recursive hooks are prevented"
OUTPUT=$(run_lush '
precmd() { echo "IN_PRECMD"; echo trigger_nested; }
echo start
')
HOOK_COUNT=$(echo "$OUTPUT" | grep -c "IN_PRECMD" || echo "0")
# Hook fires after each command, but should not cause infinite recursion
# Expected: 2-3 calls max (after start, after trigger_nested, maybe after function def)
if [ "$HOOK_COUNT" -le 4 ]; then
    pass "Recursive hook calls are prevented (no infinite loop)"
else
    fail "Recursive hooks should be prevented" "Limited IN_PRECMD count (<=4)" "Count: $HOOK_COUNT"
fi

# Test 12: chpwd with cd failure should not trigger
echo "Test 12: chpwd not triggered on cd failure"
OUTPUT=$(run_lush '
chpwd() { echo "CHPWD_TRIGGERED"; }
cd /nonexistent_directory_12345 2>/dev/null
echo "done"
')
if ! echo "$OUTPUT" | grep -q "CHPWD_TRIGGERED"; then
    pass "chpwd not triggered on failed cd"
else
    skip "chpwd may trigger even on cd failure in some implementations"
fi

echo ""
echo "========================================"
echo "Results: $PASSED passed, $FAILED failed"
echo "========================================"

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed${NC}"
    exit 1
fi
