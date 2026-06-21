#!/bin/bash
# Function Enhancements Tests
# Tests nameref variables (local -n), enhanced declare options (-g, -l, -u, -t),
# and return from sourced scripts

LUSH="${LUSH:-./build/lush}"
PASSED=0
FAILED=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

pass() {
    echo -e "${GREEN}PASS${NC}: $1"
    ((PASSED++))
}

fail() {
    echo -e "${RED}FAIL${NC}: $1"
    echo "  Expected: $2"
    echo "  Got:      $3"
    ((FAILED++))
}

run_test() {
    local name="$1"
    local script="$2"
    local expected="$3"

    local result
    result=$(echo "$script" | $LUSH 2>&1)

    if [ "$result" = "$expected" ]; then
        pass "$name"
    else
        fail "$name" "$expected" "$result"
    fi
}

run_test_contains() {
    local name="$1"
    local script="$2"
    local expected="$3"

    local result
    result=$(echo "$script" | $LUSH 2>&1)

    if echo "$result" | grep -q "$expected"; then
        pass "$name"
    else
        fail "$name" "contains '$expected'" "$result"
    fi
}

run_test_exit_code() {
    local name="$1"
    local script="$2"
    local expected_code="$3"

    echo "$script" | $LUSH >/dev/null 2>&1
    local actual_code=$?

    if [ "$actual_code" = "$expected_code" ]; then
        pass "$name"
    else
        fail "$name" "exit code $expected_code" "exit code $actual_code"
    fi
}

echo "=================================="
echo "Function Enhancements Tests"
echo "=================================="
echo ""

# ============================================================================
# SECTION 1: Nameref Variables (local -n, declare -n)
# ============================================================================
echo "--- Nameref Variables ---"

# Test 1: Basic nameref reading
run_test "Nameref: basic read through reference" \
    'target="hello"; declare -n ref=target; echo $ref' \
    "hello"

# Test 2: Nameref writing
run_test "Nameref: write through reference" \
    'target="old"; declare -n ref=target; ref="new"; echo $target' \
    "new"

# Test 3: Local nameref in function
run_test "Nameref: local -n in function" \
    'modify() { local -n r=$1; r="modified"; }; value="original"; modify value; echo $value' \
    "modified"

# Test 4: Nameref to pass variable by reference
run_test "Nameref: function modifies variable by name" \
    'double() { local -n num=$1; num=$(($num * 2)); }; x=5; double x; echo $x' \
    "10"

# Test 5: Multiple namerefs
run_test "Nameref: multiple namerefs to different targets" \
    'a=1; b=2; declare -n ra=a; declare -n rb=b; echo "$ra $rb"' \
    "1 2"

# Test 6: Nameref chain (nameref to nameref - should resolve)
run_test "Nameref: reading through chain" \
    'value=42; declare -n ref1=value; declare -n ref2=ref1; echo $ref2' \
    "42"

# Test 7: Nameref with declare -n
run_test "Nameref: declare -n syntax" \
    'x=100; declare -n y=x; echo $y' \
    "100"

# ============================================================================
# SECTION 2: Enhanced Declare Options
# ============================================================================
echo ""
echo "--- Enhanced Declare Options ---"

# Test 8: declare -l with initial value (case transformation at declare time)
run_test "Declare -l: lowercase at declare time" \
    'declare -l lower="HELLO WORLD"; echo $lower' \
    "hello world"

# Test 9: declare -u with initial value (case transformation at declare time)
run_test "Declare -u: uppercase at declare time" \
    'declare -u upper="hello world"; echo $upper' \
    "HELLO WORLD"

# Test 10: declare -l with mixed case initial value
run_test "Declare -l: mixed case at declare time" \
    'declare -l x="HeLLo WoRLd"; echo $x' \
    "hello world"

# Test 11: declare -u with mixed case initial value
run_test "Declare -u: mixed case at declare time" \
    'declare -u x="HeLLo WoRLd"; echo $x' \
    "HELLO WORLD"

# Test 12: declare without -g stays local in function
run_test "Declare: without -g stays local in function" \
    'setlocal() { declare localvar="local_value"; }; setlocal; echo ">${localvar}<"' \
    "><"

# Test 13: declare -i with initial arithmetic expression
run_test "Declare -i: integer with initial value" \
    'declare -i num=5+3; echo $num' \
    "8"

# Test 14: declare -r (readonly) sets initial value
run_test "Declare -r: readonly sets initial value" \
    'declare -r const=42; echo $const' \
    "42"

# Test 15: declare -x (export) still works
run_test "Declare -x: export attribute" \
    'declare -x exported=value; echo $exported' \
    "value"

# ============================================================================
# SECTION 3: Return from Sourced Scripts
# ============================================================================
echo ""
echo "--- Return from Sourced Scripts ---"

# Create temporary test scripts
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Test 17: Basic return from sourced script
cat > "$TMPDIR/return_basic.sh" << 'EOF'
echo "before"
return 42
echo "after"
EOF
run_test "Source return: basic return with code" \
    "source $TMPDIR/return_basic.sh; echo \"code: \$?\"" \
    "before
code: 42"

# Test 18: Return with no argument uses last status
cat > "$TMPDIR/return_no_arg.sh" << 'EOF'
false
return
EOF
result=$($LUSH -c "source $TMPDIR/return_no_arg.sh; echo \$?" 2>&1)
if [ "$result" = "1" ]; then
    pass "Source return: no argument uses last exit status"
else
    fail "Source return: no argument uses last exit status" "1" "$result"
fi

# Test 19: Return 0
cat > "$TMPDIR/return_zero.sh" << 'EOF'
return 0
EOF
result=$($LUSH -c "source $TMPDIR/return_zero.sh; echo \$?" 2>&1)
if [ "$result" = "0" ]; then
    pass "Source return: explicit return 0"
else
    fail "Source return: explicit return 0" "0" "$result"
fi

# Test 20: Nested source - inner returns, outer continues
cat > "$TMPDIR/inner.sh" << 'EOF'
echo "inner-before"
return 50
echo "inner-after"
EOF
cat > "$TMPDIR/outer.sh" << 'EOF'
echo "outer-before"
source TMPDIR/inner.sh
echo "outer-after:$?"
EOF
sed -i.bak "s|TMPDIR|$TMPDIR|g" "$TMPDIR/outer.sh" 2>/dev/null || \
    sed "s|TMPDIR|$TMPDIR|g" "$TMPDIR/outer.sh" > "$TMPDIR/outer.sh.tmp" && mv "$TMPDIR/outer.sh.tmp" "$TMPDIR/outer.sh"
run_test "Source return: nested source - outer continues after inner returns" \
    "source $TMPDIR/outer.sh" \
    "outer-before
inner-before
outer-after:50"

# Test 21: Return outside function/source fails
run_test_contains "Return: fails outside function/source" \
    'return 5' \
    "can only"

# Test 22: Return in function still works
run_test "Return: works in function" \
    'f() { echo "in-f"; return 7; echo "after"; }; f; echo "code:$?"' \
    "in-f
code:7"

# Test 23: Return with large value (masked to 0-255)
cat > "$TMPDIR/return_large.sh" << 'EOF'
return 300
EOF
result=$($LUSH -c "source $TMPDIR/return_large.sh; echo \$?" 2>&1)
expected=$((300 & 255))
if [ "$result" = "$expected" ]; then
    pass "Source return: large value masked to 0-255"
else
    fail "Source return: large value masked to 0-255" "$expected" "$result"
fi

# Test 24: Return with negative value
cat > "$TMPDIR/return_neg.sh" << 'EOF'
return -1
EOF
result=$($LUSH -c "source $TMPDIR/return_neg.sh; echo \$?" 2>&1)
# -1 & 0xFF = 255
if [ "$result" = "255" ]; then
    pass "Source return: negative value wraps to 255"
else
    fail "Source return: negative value wraps to 255" "255" "$result"
fi

# ============================================================================
# SECTION 4: Combined Features
# ============================================================================
echo ""
echo "--- Combined Features ---"

# Test 25: Nameref with lowercase attribute
run_test "Combined: nameref with case transformation" \
    'original="TEST"; declare -n ref=original; echo $ref' \
    "TEST"

# Test 26: Function using nameref and return
run_test "Combined: function with nameref and return" \
    'increment() { local -n val=$1; val=$(($val + 1)); return 0; }; n=10; increment n; echo $n' \
    "11"

# Test 27: Sourced script defines function with nameref
cat > "$TMPDIR/define_func.sh" << 'EOF'
swap() {
    local -n x=$1
    local -n y=$2
    local tmp=$x
    x=$y
    y=$tmp
}
EOF
run_test "Combined: sourced script with nameref function" \
    "source $TMPDIR/define_func.sh; a=1; b=2; swap a b; echo \"\$a \$b\"" \
    "2 1"

# Test 28: Global declaration with case attribute
run_test "Combined: declare -g -u from function" \
    'setit() { declare -g -u MYVAR="hello"; }; setit; echo $MYVAR' \
    "HELLO"

# ============================================================================
# Summary
# ============================================================================
echo ""
echo "=================================="
echo "Results: $PASSED passed, $FAILED failed"
echo "=================================="

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
