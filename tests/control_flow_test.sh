#!/bin/bash
# Control Flow Extensions Tests
# Tests case fall-through (;&), case continue (;;&), select loop, and time keyword

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

echo "============================================="
echo "Control Flow Extensions Tests"
echo "============================================="
echo

# =============================================
# Case Fall-Through Tests (;&)
# =============================================
echo "--- Case Fall-Through (;&) ---"

run_test "case ;& basic fall-through" '
case "b" in
    a) echo "a" ;&
    b) echo "b" ;&
    c) echo "c" ;;
esac
' "b
c"

run_test "case ;& from first match" '
case "a" in
    a) echo "a" ;&
    b) echo "b" ;&
    c) echo "c" ;;
esac
' "a
b
c"

run_test "case ;& stops at ;;" '
case "a" in
    a) echo "a" ;&
    b) echo "b" ;;
    c) echo "c" ;;
esac
' "a
b"

run_test "case ;& with default" '
case "x" in
    a) echo "a" ;&
    b) echo "b" ;;
    *) echo "default" ;;
esac
' "default"

run_test "case ;& at end of case" '
case "x" in
    x) echo "x" ;&
esac
echo "done"
' "x
done"

# =============================================
# Case Continue Tests (;;&)
# =============================================
echo
echo "--- Case Continue (;;&) ---"

run_test "case ;;& basic continue" '
case "ab" in
    *a*) echo "has a" ;;&
    *b*) echo "has b" ;;&
    *c*) echo "has c" ;;
esac
' "has a
has b"

run_test "case ;;& matches multiple patterns" '
case "abc" in
    *a*) echo "has a" ;;&
    *b*) echo "has b" ;;&
    *c*) echo "has c" ;;
esac
' "has a
has b
has c"

run_test "case ;;& skips non-matching" '
case "ac" in
    *a*) echo "has a" ;;&
    *b*) echo "has b" ;;&
    *c*) echo "has c" ;;
esac
' "has a
has c"

run_test "case ;;& with default" '
case "x" in
    *a*) echo "has a" ;;&
    *b*) echo "has b" ;;&
    *) echo "default" ;;
esac
' "default"

run_test "case ;;& all patterns match" '
case "test" in
    *) echo "match 1" ;;&
    t*) echo "match 2" ;;&
    *t) echo "match 3" ;;
esac
' "match 1
match 2
match 3"

# =============================================
# Mixed Terminators
# =============================================
echo
echo "--- Mixed Terminators ---"

run_test "case mixed ;; and ;&" '
case "2" in
    1) echo "one" ;;
    2) echo "two" ;&
    3) echo "three" ;;
    *) echo "other" ;;
esac
' "two
three"

run_test "case mixed ;; and ;;&" '
case "12" in
    *1*) echo "has 1" ;;&
    *2*) echo "has 2" ;;
    *3*) echo "has 3" ;;
esac
' "has 1
has 2"

run_test "case ;& then ;;&" '
case "a" in
    a) echo "a" ;&
    b) echo "b" ;;&
    c) echo "c" ;;
    *) echo "default" ;;
esac
' "a
b
default"

# =============================================
# Select Loop Tests
# =============================================
echo
echo "--- Select Loop ---"

# Test select with valid input
result=$(printf "2\n" | $LUSH -c '
select opt in one two three; do
    echo "Selected: $opt"
    break
done
' 2>&1)

if echo "$result" | grep -q "Selected: two"; then
    pass "select valid selection"
else
    fail "select valid selection" "Selected: two" "$result"
fi

# Test select sets REPLY
result=$(printf "2\n" | $LUSH -c '
select opt in a b c; do
    echo "REPLY=$REPLY"
    break
done
' 2>&1)

if echo "$result" | grep -q "REPLY=2"; then
    pass "select sets REPLY"
else
    fail "select sets REPLY" "REPLY=2" "$result"
fi

# Test select with invalid input (sets variable to empty)
result=$(printf "99\n2\n" | $LUSH -c '
select opt in a b c; do
    if [ -z "$opt" ]; then
        echo "invalid"
    else
        echo "valid: $opt"
        break
    fi
done
' 2>&1)

if echo "$result" | grep -q "invalid" && echo "$result" | grep -q "valid: b"; then
    pass "select handles invalid input"
else
    fail "select handles invalid input" "invalid then valid: b" "$result"
fi

# Test select with PS3
result=$(printf "1\n" | $LUSH -c '
PS3="Choose: "
select opt in x; do
    break
done
' 2>&1)

if echo "$result" | grep -q "Choose:"; then
    pass "select uses PS3 prompt"
else
    fail "select uses PS3 prompt" "Choose:" "$result"
fi

# =============================================
# Time Keyword Tests
# =============================================
echo
echo "--- Time Keyword ---"

# Test time basic
result=$(echo 'time echo hello' | $LUSH 2>&1)
if echo "$result" | grep -q "real" && echo "$result" | grep -q "user" && echo "$result" | grep -q "sys"; then
    pass "time shows real/user/sys"
else
    fail "time shows real/user/sys" "real/user/sys output" "$result"
fi

# Test time -p (POSIX format)
result=$(echo 'time -p echo hello' | $LUSH 2>&1)
if echo "$result" | grep -qE "^real [0-9]"; then
    pass "time -p POSIX format"
else
    fail "time -p POSIX format" "POSIX format output" "$result"
fi

# Test time with pipeline
result=$(echo 'time echo test | cat' | $LUSH 2>&1)
if echo "$result" | grep -q "real"; then
    pass "time with pipeline"
else
    fail "time with pipeline" "timing output" "$result"
fi

# Test time preserves exit status
result=$($LUSH -c 'time false; echo $?' 2>&1 | tail -1)
if [ "$result" = "1" ]; then
    pass "time preserves exit status"
else
    fail "time preserves exit status" "1" "$result"
fi

# =============================================
# Edge Cases
# =============================================
echo
echo "--- Edge Cases ---"

run_test "case empty pattern body with ;&" '
case "a" in
    a) ;&
    b) echo "b" ;;
esac
' "b"

run_test "case with variable in pattern" '
pat="hello"
case "hello" in
    $pat) echo "matched" ;;
esac
' "matched"

run_test "nested case with fall-through" '
case "x" in
    x)
        case "a" in
            a) echo "inner a" ;&
            b) echo "inner b" ;;
        esac
        ;;
esac
' "inner a
inner b"

# =============================================
# Summary
# =============================================
echo
echo "============================================="
echo "Test Summary"
echo "============================================="
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"
echo

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
