#!/bin/bash
# LLE Architectural Compliance Check - Fresh Start Edition
# Automated verification for fresh start implementation
#
# This script grows with the codebase:
# - Week 1: Terminal capability detection only
# - Week 2: + Display integration checks
# - Week 3+: Full architectural compliance
#
# Zero tolerance for architectural violations.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

VIOLATIONS=0
WARNINGS=0
CHECKS=0

echo "======================================================================="
echo "LLE Architectural Compliance Check - Fresh Start"
echo "Version: 1.0.0 (Week 1)"
echo "Date: $(date)"
echo "======================================================================="
echo ""

# Function to report violation
report_violation() {
    local check_name="$1"
    local details="$2"
    echo -e "${RED}FAIL VIOLATION${NC}: $check_name"
    echo "  Details: $details"
    echo ""
    ((VIOLATIONS++))
}

# Function to report warning
report_warning() {
    local check_name="$1"
    local details="$2"
    echo -e "${YELLOW}WARNING WARNING${NC}: $check_name"
    echo "  Details: $details"
    echo ""
    ((WARNINGS++))
}

# Function to report pass
report_pass() {
    local check_name="$1"
    echo -e "${GREEN}OK PASS${NC}: $check_name"
    ((CHECKS++))
}

echo "Checking LLE source directory: src/lle/"
echo ""

# Check if src/lle exists
if [ ! -d "src/lle" ]; then
    echo "ERROR: src/lle/ directory not found"
    exit 1
fi

# Count source files
SOURCE_FILES=$(find src/lle -name "*.c" 2>/dev/null | wc -l)
HEADER_FILES=$(find include/lle -name "*.h" 2>/dev/null | wc -l)
echo "Found $SOURCE_FILES LLE source files"
echo "Found $HEADER_FILES LLE header files"
echo ""

# ============================================================================
# WEEK 1 CHECKS - Terminal Capability Detection
# ============================================================================

echo "=== WEEK 1: Terminal Capability Detection Checks ==="
echo ""

# CHECK 1: No terminal state queries during operation
echo "Check 1: No terminal state queries in capability detection..."
QUERY_PATTERNS="write.*fd.*\\\x1b\|printf.*\\\x1b\|fprintf.*\\\x1b"
if find src/lle/terminal -name "*.c" -exec grep -H "$QUERY_PATTERNS" {} \; 2>/dev/null | grep -v "//"; then
    report_violation "Terminal queries detected" \
        "Terminal capability detection MUST NOT query terminal state. Use environment/ioctl only."
else
    report_pass "No terminal state queries"
fi

# CHECK 2: No escape sequences in terminal detection code
echo "Check 2: No escape sequences in terminal code..."
if grep -r '\\x1b\|\\033' src/lle/terminal/*.c 2>/dev/null | grep -v "//"; then
    report_violation "Escape sequences in terminal code" \
        "Terminal code MUST NOT contain escape sequences. Detection is environment-based only."
else
    report_pass "No escape sequences in terminal code"
fi

# CHECK 3: Environment variable usage (should be present)
echo "Check 3: Environment variable usage for detection..."
if grep -q "getenv" src/lle/terminal/*.c 2>/dev/null; then
    report_pass "Environment variables used for detection"
else
    report_warning "No environment variable usage found" \
        "Terminal detection should use TERM, COLORTERM, etc."
fi

# CHECK 4: ioctl usage for geometry (should be present)
echo "Check 4: ioctl TIOCGWINSZ for terminal geometry..."
if grep -q "TIOCGWINSZ\|ioctl" src/lle/terminal/*.c 2>/dev/null; then
    report_pass "ioctl used for terminal geometry"
else
    report_warning "No ioctl usage found" \
        "Terminal geometry should use ioctl TIOCGWINSZ"
fi

# CHECK 5: No direct terminal writes
echo "Check 5: No direct terminal writes..."
if grep -r "write.*STDOUT_FILENO\|write.*STDERR_FILENO" src/lle/*.c src/lle/*/*.c 2>/dev/null | grep -v "//"; then
    report_violation "Direct terminal writes detected" \
        "LLE code MUST NOT write directly to terminal. Use Lush display system."
else
    report_pass "No direct terminal writes"
fi

# CHECK 6: Proper header includes
echo "Check 6: Public API headers exist..."
if [ -f "include/lle/terminal.h" ]; then
    report_pass "Public terminal.h header exists"
else
    report_violation "Missing public header" \
        "include/lle/terminal.h must exist"
fi

# CHECK 7: No TODO markers in core functionality
echo "Check 7: No TODO markers in core functions..."
TODO_COUNT=$(grep -r "TODO\|FIXME\|STUB\|XXX" src/lle/*.c src/lle/*/*.c 2>/dev/null | wc -l)
if [ "$TODO_COUNT" -gt 0 ]; then
    report_violation "TODO markers found ($TODO_COUNT)" \
        "Core functionality must be complete. Found $TODO_COUNT TODO/FIXME/STUB markers."
else
    report_pass "No TODO markers in core code"
fi

# CHECK 8: No "For now" or "Phase 2" comments
echo "Check 8: No placeholder implementation comments..."
if grep -r "For now\|Phase 2\|Phase 3\|TODO_SPEC\|will.*later" src/lle/*.c src/lle/*/*.c 2>/dev/null | grep -v test; then
    report_violation "Placeholder comments found" \
        "Code must be complete. No 'For now' or 'Phase X' comments allowed."
else
    report_pass "No placeholder implementation comments"
fi

# ============================================================================
# WEEK 2+ CHECKS - Will be added as we implement
# ============================================================================

echo ""
echo "=== FUTURE CHECKS (Not yet applicable) ==="
echo ""

# These will activate when we implement display integration (Week 2)
if [ -d "src/lle/display" ] && [ "$(find src/lle/display -name '*.c' | wc -l)" -gt 0 ]; then
    echo "Week 2 checks would run here (display integration)"
else
    echo "Week 2: Display integration not yet implemented - checks skipped"
fi

# These will activate when we implement buffer management (Week 3)
if [ -d "src/lle/buffer" ] && [ "$(find src/lle/buffer -name '*.c' | wc -l)" -gt 0 ]; then
    echo "Week 3 checks would run here (buffer management)"
else
    echo "Week 3: Buffer management not yet implemented - checks skipped"
fi

# ============================================================================
# SUMMARY
# ============================================================================

echo ""
echo "======================================================================="
echo "COMPLIANCE CHECK SUMMARY"
echo "======================================================================="
echo "Checks performed: $CHECKS"
echo "Violations: $VIOLATIONS"
echo "Warnings: $WARNINGS"
echo ""

if [ $VIOLATIONS -eq 0 ]; then
    echo -e "${GREEN}OK COMPLIANCE CHECK PASSED${NC}"
    echo ""
    echo "All architectural requirements met for current implementation stage."
    if [ $WARNINGS -gt 0 ]; then
        echo "Note: $WARNINGS warning(s) present but not blocking."
    fi
    exit 0
else
    echo -e "${RED}FAIL COMPLIANCE CHECK FAILED${NC}"
    echo ""
    echo "Found $VIOLATIONS architectural violation(s)."
    echo "These MUST be fixed before committing code."
    exit 1
fi
